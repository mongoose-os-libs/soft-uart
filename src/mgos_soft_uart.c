#include "mgos.h"
#include "mgos_soft_uart.h"

#ifndef MGOS_SOFT_UART_MAX_NUM
#error Please define MGOS_SOFT_UART_MAX_NUM
#endif

#define MG_SOFT_UART_DISPATCH_TIMEOUT 50

enum mg_soft_uart_rx_add_bit {
  MG_SOFT_UART_RX_ADD_BIT_OK = 0,    // 00
  MG_SOFT_UART_RX_ADD_BIT_ERROR = 1, // 01
  MG_SOFT_UART_RX_ADD_BIT_STOP = 2,  // 10
};

struct mg_soft_uart {
  int uart_no;
  struct mgos_soft_uart_config uart_cfg;
  struct mgos_config_soft_uart_port port_cfg;

  int8_t data_frame_len;

  bool rx_enabled;
  struct mbuf rx_buf;
  mgos_uart_dispatcher_t dispatcher_cb;
  void* dispatcher_data;
  uint32_t bit_duration;     //microseconds
  int64_t rx_last_data_ticks;
  int8_t rx_data_bits;
  int8_t rx_data_bit_idx;
  int8_t rx_parity_count;
  int8_t rx_parity_err;
  int8_t rx_stop_count;
  int8_t rx_idle;
  mgos_timer_id rx_timer_id;
  mgos_timer_id dispatch_timer_id;

  struct mbuf tx_buf;
};

static struct mg_soft_uart s_uart_state[MGOS_SOFT_UART_MAX_NUM];

struct mg_soft_uart* mg_soft_uart_get(int uart_no) {
  return ((uart_no >= 0 && uart_no < MGOS_SOFT_UART_MAX_NUM) ? &s_uart_state[uart_no] : NULL);
}

const struct mgos_config_soft_uart_port* mg_soft_uart_get_sys_config(int uart_no) {
  const struct mgos_config_soft_uart *cfg = mgos_sys_config_get_soft_uart();
  return &((const struct mgos_config_soft_uart_port*)cfg)[uart_no];
}

void mg_soft_uart_schedule_dispatcher(struct mg_soft_uart* uart) {
  if (uart && uart->dispatcher_cb) {
    uart->dispatcher_cb(uart->uart_no, uart->dispatcher_data);
  }
}

enum mg_soft_uart_rx_add_bit mg_soft_uart_rx_add_bit(struct mg_soft_uart* uart, bool bit_val) {
  enum mg_soft_uart_rx_add_bit result = MG_SOFT_UART_RX_ADD_BIT_OK;

  if (uart->rx_data_bit_idx < uart->uart_cfg.num_data_bits) {
    uart->rx_data_bits += (bit_val ? 1 : 0) << uart->rx_data_bit_idx;
    if (bit_val) ++uart->rx_parity_count;
  }

  ++uart->rx_data_bit_idx;

  if (uart->rx_data_bit_idx > uart->uart_cfg.num_data_bits) {   
    if (uart->uart_cfg.parity != MGOS_UART_PARITY_NONE &&
        uart->rx_data_bit_idx == (uart->uart_cfg.num_data_bits + 1)) {
      // I'm receiving the parity bit right now
      bool bits_are_even = (((uart->rx_parity_count + (bit_val ? 1 : 0)) % 2) == 0);
      if (((uart->uart_cfg.parity == MGOS_UART_PARITY_EVEN) && !bits_are_even) ||
          ((uart->uart_cfg.parity == MGOS_UART_PARITY_ODD) && bits_are_even)) {
        uart->rx_parity_err = 1;
      }
    } else {
      // this should be the STOP bit
      if (bit_val)
        ++uart->rx_stop_count;

      if (uart->rx_data_bit_idx == uart->data_frame_len) {
        result = MG_SOFT_UART_RX_ADD_BIT_STOP;
        if (uart->rx_parity_err == 1 || uart->rx_stop_count != (uart->uart_cfg.stop_bits == MGOS_UART_STOP_BITS_1 ? 1 : 2)) {
          result |= MG_SOFT_UART_RX_ADD_BIT_ERROR;
        }
      }
    }
  }

  return result;
}

static void mg_soft_uart_rx_dispatch_cb(void *arg) {
  struct mg_soft_uart* uart = (struct mg_soft_uart*)arg;
  if (uart->rx_last_data_ticks != 0 && (mgos_uptime_micros() - uart->rx_last_data_ticks) > uart->bit_duration) {
    // reset some values before invoking the dispatcher
    uart->rx_last_data_ticks = 0; 
    uart->rx_idle = 1;

    mg_soft_uart_schedule_dispatcher(uart);
  }
}

static void mg_soft_uart_rx_data_bits_timer_cb(void *arg) {
  struct mg_soft_uart* uart = (struct mg_soft_uart*)arg;

  enum mg_soft_uart_rx_add_bit res = mg_soft_uart_rx_add_bit(uart,
    mgos_gpio_read(uart->port_cfg.rx_gpio_pin));

  if ((res & MG_SOFT_UART_RX_ADD_BIT_STOP) == MG_SOFT_UART_RX_ADD_BIT_STOP) {
    if ((res & MG_SOFT_UART_RX_ADD_BIT_ERROR) != MG_SOFT_UART_RX_ADD_BIT_ERROR) {
      mbuf_append(&uart->rx_buf, &uart->rx_data_bits, 1);
      uart->rx_last_data_ticks = mgos_uptime_micros();
    }
    
    mgos_clear_timer(uart->rx_timer_id);
    uart->rx_timer_id = MGOS_INVALID_TIMER_ID;
    uart->rx_data_bit_idx = -1;
  }
}

void mg_soft_uart_rx_low_handler(int pin, void *arg) {
  struct mg_soft_uart* uart = (struct mg_soft_uart*)arg;
  if (uart->rx_data_bit_idx == -1) {
    // START bit detected
    if (uart->rx_timer_id == MGOS_INVALID_TIMER_ID) {
      mgos_usleep(uart->bit_duration / 2);
      uart->rx_timer_id = mgos_set_hw_timer(uart->bit_duration, 
        MGOS_TIMER_REPEAT, mg_soft_uart_rx_data_bits_timer_cb, uart);
    }
    
    if (uart->rx_timer_id != MGOS_INVALID_TIMER_ID) {
      // START receiving data...
      uart->rx_data_bit_idx = 0;
      uart->rx_data_bits = 0;
      uart->rx_parity_count = 0;
      uart->rx_parity_err = 0;
      uart->rx_stop_count = 0;
      uart->rx_idle = 0;
    } else {
      LOG(LL_ERROR, ("Unable to start RX timer on SOFT-UART%d", uart->uart_no));
    }
  }
}

void mgos_soft_uart_config_set_defaults(int uart_no, struct mgos_soft_uart_config *cfg) {
  if (uart_no >= 0 && uart_no < MGOS_SOFT_UART_MAX_NUM && cfg != NULL) {
    memset(cfg, 0, sizeof(*cfg));
    cfg->baud_rate = 9600;
    cfg->num_data_bits = 8;
    cfg->parity = MGOS_UART_PARITY_NONE;
    cfg->stop_bits = MGOS_UART_STOP_BITS_1;
    cfg->rx_buf_size = cfg->tx_buf_size = 256;
  }
}

bool mgos_soft_uart_configure(int uart_no, const struct mgos_soft_uart_config *cfg) {
  struct mg_soft_uart* uart = mg_soft_uart_get(uart_no);
  if (uart && cfg) {
    if (cfg->stop_bits != MGOS_UART_STOP_BITS_1_5) {
      memcpy(&uart->uart_cfg, cfg, sizeof(*cfg));
      mbuf_init(&uart->rx_buf, uart->uart_cfg.rx_buf_size);
      mbuf_init(&uart->tx_buf, uart->uart_cfg.tx_buf_size);
      // set bit_duration (microseconds)
      uart->bit_duration = (uint32_t)(1000000 / uart->uart_cfg.baud_rate);
      // set data_frame_len
      uart->data_frame_len = (uart->uart_cfg.num_data_bits
        + (uart->uart_cfg.parity == MGOS_UART_PARITY_NONE ? 0 : 1)
        + (uart->uart_cfg.stop_bits == MGOS_UART_STOP_BITS_1 ? 1 : 2));

      mg_soft_uart_schedule_dispatcher(uart);
      return true;
    }

    LOG(LL_ERROR, ("Invalid SOFT-UART%d configuration: [stop_bits] must be 1 or 2", uart_no));
  }
  return false;
}

bool mgos_soft_uart_config_get(int uart_no, struct mgos_soft_uart_config *cfg) {
  struct mg_soft_uart* uart = mg_soft_uart_get(uart_no);
  if (uart && cfg) {
    /* A way of telling if the UART has been configured. */
    if (uart->uart_cfg.rx_buf_size != 0 && uart->uart_cfg.tx_buf_size != 0) {
      memcpy(cfg, &uart->uart_cfg, sizeof(*cfg));
      return true;
    }
  }
  return false;
}

void mgos_soft_uart_set_dispatcher(int uart_no, mgos_uart_dispatcher_t cb, void *arg) {
  struct mg_soft_uart* uart = mg_soft_uart_get(uart_no);
  if (uart) {
    uart->dispatcher_cb = cb;
    uart->dispatcher_data = arg;
  }
}

size_t mgos_soft_uart_read(int uart_no, void *buf, size_t len) {
  if (mgos_soft_uart_is_rx_enabled(uart_no)) {
    struct mg_soft_uart* uart = mg_soft_uart_get(uart_no);
    if (uart) {
      size_t tr = MIN(len, uart->rx_buf.len);
      memcpy(buf, uart->rx_buf.buf, tr);
      mbuf_remove(&uart->rx_buf, tr);
      return tr;
    }
  }
  return 0;
}

size_t mgos_soft_uart_read_mbuf(int uart_no, struct mbuf *mb, size_t len) {
  if (!mgos_soft_uart_is_rx_enabled(uart_no)) return 0;
  size_t nr = MIN(len, mgos_soft_uart_read_avail(uart_no));
  if (nr > 0) {
    size_t free_bytes = mb->size - mb->len;
    if (free_bytes < nr) {
      mbuf_resize(mb, mb->len + nr);
    }
    nr = mgos_soft_uart_read(uart_no, mb->buf + mb->len, nr);
    mb->len += nr;
  }
  return nr;
}

size_t mgos_soft_uart_read_avail(int uart_no) {
  struct mg_soft_uart* uart = mg_soft_uart_get(uart_no);
  // If I'm not receiving any data, I can return current available bytes
  return ((uart && (uart->rx_idle == 1)) ? uart->rx_buf.len : 0);
}

bool mgos_soft_uart_set_rx_enabled(int uart_no, bool enabled) {
  struct mg_soft_uart* uart = mg_soft_uart_get(uart_no);
  if (uart && (uart->port_cfg.rx_gpio_pin >= 0)) {
    if (uart->rx_enabled != enabled) {
      if (!enabled) {
        if (!mgos_gpio_disable_int(uart->port_cfg.rx_gpio_pin)) {
          LOG(LL_ERROR, ("Unable to disable RX on SOFT-UART%d", uart_no));
          return false;
        }
        // start the dispatch timer
        mgos_clear_timer(uart->dispatch_timer_id);
        uart->dispatch_timer_id = MGOS_INVALID_TIMER_ID;
      } else {
        uart->dispatch_timer_id = mgos_set_timer(MG_SOFT_UART_DISPATCH_TIMEOUT,
          MGOS_TIMER_REPEAT, mg_soft_uart_rx_dispatch_cb, uart);

        if (uart->dispatch_timer_id == MGOS_INVALID_TIMER_ID) {
          LOG(LL_ERROR, ("Unable to start the dispatch timer on SOFT-UART%d", uart_no));
          return false;
        }
        if (!mgos_gpio_enable_int(uart->port_cfg.rx_gpio_pin)) {
          mgos_clear_timer(uart->dispatch_timer_id);
          uart->dispatch_timer_id = MGOS_INVALID_TIMER_ID;
          LOG(LL_ERROR, ("Unable to enable RX on SOFT-UART%d", uart_no));
          return false;
        }
      }
      uart->rx_enabled = enabled;
    }
    return true;  
  }
  return false;
}

bool mgos_soft_uart_is_rx_enabled(int uart_no) {
  struct mg_soft_uart* uart = mg_soft_uart_get(uart_no);
  return (uart ? uart->rx_enabled : false);
}

size_t mgos_soft_uart_write(int uart_no, const void *buf, size_t len) {
  size_t written = 0;
  struct mg_soft_uart* uart = mg_soft_uart_get(uart_no);
  if (uart) {
    while (written < len) {
      size_t nw = MIN(len - written, mgos_soft_uart_write_avail(uart_no));
      mbuf_append(&uart->tx_buf, ((const char *) buf) + written, nw);
      written += nw;
      if (written < len)
        mgos_soft_uart_flush(uart_no);
    }
    mgos_soft_uart_flush(uart_no);
    if (mgos_soft_uart_write_avail(uart_no) > 0)
      mg_soft_uart_schedule_dispatcher(uart);
  }
  return written;
}

int mgos_soft_uart_printf(int uart_no, const char *fmt, ...) {
  int len = 0;
  struct mg_soft_uart* uart = mg_soft_uart_get(uart_no);
  if (uart) {
    va_list ap;
    char buf[100], *data = buf;
    va_start(ap, fmt);
    len = mg_avprintf(&data, sizeof(buf), fmt, ap);
    va_end(ap);
    if (len > 0) {
      len = mgos_soft_uart_write(uart_no, data, len);
    }
    if (data != buf) {
      free(data);
    }
  }
  return len;
}

void mg_soft_uart_flush_bits(struct mg_soft_uart* uart, uint8_t data) {
  // START bit
  mgos_gpio_write(uart->port_cfg.tx_gpio_pin, false);
  mgos_usleep(uart->bit_duration);

  // DATA
  uint8_t parity = 0;
  for (uint8_t b = 0; b < uart->uart_cfg.num_data_bits; ++b, data >>= 1) {
    if (data & 0x01) {
      ++parity;
      mgos_gpio_write(uart->port_cfg.tx_gpio_pin, true);
    } else {
      mgos_gpio_write(uart->port_cfg.tx_gpio_pin, false);
    }
    mgos_usleep(uart->bit_duration);
  }
  
  // PARITY bit (optional)
  if (uart->uart_cfg.parity != MGOS_UART_PARITY_NONE) {
    if ((parity % 2) == 0) {
      parity = (uart->uart_cfg.parity == MGOS_UART_PARITY_ODD ? true : false);
    } else {
      parity = (uart->uart_cfg.parity == MGOS_UART_PARITY_EVEN ? true : false);
    }
    mgos_gpio_write(uart->port_cfg.tx_gpio_pin, parity);
    mgos_usleep(uart->bit_duration);
  }

  // STOP bit/s
  mgos_gpio_write(uart->port_cfg.tx_gpio_pin, true);
  mgos_usleep(uart->bit_duration * (uart->uart_cfg.stop_bits == MGOS_UART_STOP_BITS_1 ? 1 : 2));
}

void mgos_soft_uart_flush(int uart_no) {
  struct mg_soft_uart* uart = mg_soft_uart_get(uart_no);
  if (uart) {
    mgos_ints_disable();
    
    for (size_t idx = 0; idx < uart->tx_buf.len; ++idx) {
      mg_soft_uart_flush_bits(uart, *((uint8_t *)(uart->tx_buf.buf) + idx));
    }

    mgos_ints_enable();
    mbuf_remove(&uart->tx_buf, uart->tx_buf.len);
  }
}

size_t mgos_soft_uart_write_avail(int uart_no) {
  struct mg_soft_uart* uart = mg_soft_uart_get(uart_no);
  if (uart == NULL || ((int) uart->tx_buf.len) > uart->uart_cfg.tx_buf_size) return 0;
  return uart->uart_cfg.tx_buf_size - uart->tx_buf.len;
}


bool mg_soft_uart_init() {
  for (int uart_no = 0; uart_no < MGOS_SOFT_UART_MAX_NUM; ++uart_no) {
    struct mg_soft_uart* uart = mg_soft_uart_get(uart_no);
    if (uart) {
      // initialize RX
      if (uart->port_cfg.rx_gpio_pin >= 0) {
        if (!mgos_gpio_setup_input(uart->port_cfg.rx_gpio_pin, uart->port_cfg.rx_gpio_pull_up)) {
          LOG(LL_ERROR, ("SOFT-UART%d: unable to set GPIO %d as RX pin", uart_no, uart->port_cfg.rx_gpio_pin));
          return false;
        }
        if (!mgos_gpio_set_int_handler_isr(uart->port_cfg.rx_gpio_pin, MGOS_GPIO_INT_EDGE_NEG,
                                           mg_soft_uart_rx_low_handler, uart)) {
          LOG(LL_ERROR, ("SOFT-UART%d: unable to set RX interrupt on GPIO %d", uart_no, uart->port_cfg.rx_gpio_pin));
          return false;
        }
      }
      // initialize TX
      if (uart->port_cfg.tx_gpio_pin >= 0) {
        if (!mgos_gpio_setup_output(uart->port_cfg.tx_gpio_pin, true)) {
          LOG(LL_ERROR, ("SOFT-UART%d: unable to set GPIO %d as TX pin", uart_no, uart->port_cfg.tx_gpio_pin));
          return false;
        }
      }
      LOG(LL_DEBUG, ("SOFT-UART%d successfully initialized (RX pin %d, TX pin %d)",
        uart_no, uart->port_cfg.rx_gpio_pin, uart->port_cfg.tx_gpio_pin));
    }
  }
  return true;
}

bool mg_soft_uart_init_cfgs() {
  for (int uart_no = 0; uart_no < MGOS_SOFT_UART_MAX_NUM; ++uart_no) {
    struct mg_soft_uart* uart = mg_soft_uart_get(uart_no);
    if (uart) {
      // reset uart data
      memset(uart, 0, sizeof(struct mg_soft_uart));

      uart->uart_no = uart_no;
      uart->rx_timer_id = MGOS_INVALID_TIMER_ID;
      uart->dispatch_timer_id = MGOS_INVALID_TIMER_ID;
      uart->rx_data_bit_idx = -1;
      uart->rx_idle = 1;

      // set port 'n' configuration
      memcpy(&uart->port_cfg, mg_soft_uart_get_sys_config(uart_no),
        sizeof(struct mgos_config_soft_uart_port));
    }
  }
  return true;
}

bool mgos_soft_uart_init(void) {
  if (!mg_soft_uart_init_cfgs()) {
    return false;
  }

  if (!mg_soft_uart_init()) {
    return false;
  }

  return true;
}