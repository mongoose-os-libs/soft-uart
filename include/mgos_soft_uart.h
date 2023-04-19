/*
 * Copyright (c) 2023 DIY356
 * All rights reserved
 *
 * Licensed under the Apache License, Version 2.0 (the ""License"");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an ""AS IS"" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef MGOS_SOFT_UART_H_
#define MGOS_SOFT_UART_H_

#include <stdbool.h>
#include <stdlib.h>
#include "mgos_uart.h"

#ifdef __cplusplus
extern "C" {
#endif

#define MGOS_SOFT_UART_MAX_NUM 1

struct mgos_soft_uart_config {
  int baud_rate;                      /* Baud rate. Default: 9600 */
  int num_data_bits;                  /* Number of data bits, 5-8. Default: 8 */
  enum mgos_uart_parity parity;       /* Parity. Default: none */
  enum mgos_uart_stop_bits stop_bits; /* Number of stop bits. Default: 1 */

  /* Size of the Rx buffer, default: 256 */
  int rx_buf_size;

  /* Size of the Tx buffer, default: 256 */
  int tx_buf_size;
};

void mgos_soft_uart_config_set_defaults(int uart_no, struct mgos_soft_uart_config *cfg);

bool mgos_soft_uart_config_get(int uart_no, struct mgos_soft_uart_config *cfg);

bool mgos_soft_uart_configure(int uart_no, const struct mgos_soft_uart_config *cfg);

void mgos_soft_uart_set_dispatcher(int uart_no, mgos_uart_dispatcher_t cb, void *arg);

size_t mgos_soft_uart_read(int uart_no, void *buf, size_t len);
size_t mgos_soft_uart_read_mbuf(int uart_no, struct mbuf *mb, size_t len);

size_t mgos_soft_uart_read_avail(int uart_no);

bool mgos_soft_uart_set_rx_enabled(int uart_no, bool enabled);

bool mgos_soft_uart_is_rx_enabled(int uart_no);

size_t mgos_soft_uart_write(int uart_no, const void *buf, size_t len);

size_t mgos_soft_uart_write_avail(int uart_no);

int mgos_soft_uart_printf(int uart_no, const char *fmt, ...);

void mgos_soft_uart_flush(int uart_no);

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* MGOS_SOFT_UART_H_ */