# Software-UART Library
## Overview
Software-UART library for [Mongoose OS](https://mongoose-os.com/).
> :bulb: This library is strongly optimized and it uses interrupts and hardware timers to do not block CPU while receiving data.

Use this library to add new UART ports, in addition to the hardware ones, on the MCUs listed in the table below.

|MCU|SOFT-UART0 (Rx/Tx pins)|SOFT-UART1|Notes|
|--|:--:|:--:|--|
|ESP8266|13/15|--||
|ESP32|--|--|_Not yet tested._|
## Configuration
The library adds the `soft_uart` section to the device configuration:
```javascript
{
  "port":                       // SOFT-SOFT-UART0 port settings
  {
    "rx_gpio_pin": ,            // Default Rx pin
    "rx_gpio_pull_up": true,    // True if Rx GPIO is pulled up
    "tx_gpio_pin": ,            // Default Tx pin
  }
}
```
## C/C++ APIs Reference
### mgos_soft_uart_config
```c
struct mgos_soft_uart_config {
  int baud_rate;
  int num_data_bits;
  enum mgos_uart_parity parity;
  enum mgos_uart_stop_bits stop_bits;
  int rx_buf_size;
  int tx_buf_size;
};
```
UART configuration parameters.

|Field||
|--|--|
|baud_rate|Baud rate. Default: 9600.|
|num_data_bits|Number of data bits, 5-8. Default: 8.|
|parity|Parity. Default: `MGOS_UART_PARITY_NONE`.|
|stop_bits|Number of stop bits. Default: `MGOS_UART_STOP_BITS_1`. Value `MGOS_UART_STOP_BITS_1_5` is not allowed.|
|rx_buf_size|Size of the Rx buffer. Default: 256.|
|tx_buf_size|Size of the Tx buffer. Dfault: 256.|

### mgos_soft_uart_configure
```c
bool mgos_soft_uart_configure(int uart_no, const struct mgos_soft_uart_config *cfg);
```
Apply given UART [configuration](#mgos_soft_uart_config).

|Parameter||
|--|--|
|uart_no|UART number.|
|cfg|[Configuration structure](#mgos_soft_uart_config).|

Example:
```c
#include "mgos_soft_uart.h"

int uart_no = 0;
struct mgos_soft_uart_config ucfg;
mgos_soft_uart_config_set_defaults(uart_no, &ucfg);

ucfg.baud_rate = 9600;
ucfg.rx_buf_size = 1500;
ucfg.tx_buf_size = 1500;

if (!mgos_soft_uart_configure(uart_no, &ucfg)) {
  LOG(LL_ERROR, ("Failed to configure SOFT-UART%d", uart_no));
}
```
### mgos_soft_uart_config_set_defaults
```c
void mgos_soft_uart_config_set_defaults(int uart_no, struct mgos_soft_uart_config *cfg);
```
Fill provided `cfg` [structure](#mgos_soft_uart_config) with the default values. See example above.

|Parameter||
|--|--|
|uart_no|UART number.|
|cfg|[Configuration structure](#mgos_soft_uart_config).|
### mgos_soft_uart_config_get
```c|UART number.|
bool mgos_soft_uart_config_get(int uart_no, struct mgos_soft_uart_config *cfg);
```
Fill provided cfg structure with the current UART config. Returns false if the specified UART has not bee configured yet.

|Parameter||
|--|--|
|uart_no|UART number.|
|cfg|[Configuration structure](#mgos_soft_uart_config).|
### mgos_soft_uart_set_dispatcher
```c
void mgos_soft_uart_set_dispatcher(int uart_no, mgos_uart_dispatcher_t cb, void *arg);
```
Set UART dispatcher: a callback which gets called when there is data in the input buffer or space available in the output buffer.

|Parameter||
|--|--|
|uart_no|UART number.|
|cb|Dispatcher's [callback](https://mongoose-os.com/docs/mongoose-os/api/core/mgos_uart.h.md#mgos_uart_dispatcher_t).|
|arg|Callback arguments or `NULL`.|
### mgos_soft_uart_read
```c
size_t mgos_soft_uart_read(int uart_no, void *buf, size_t len);
```
Read data from UART input buffer. Note: unlike write, read will not block if there are not enough bytes in the input buffer.

|Parameter||
|--|--|
|uart_no|UART number.|
|buf|Buffer.|
|len|Len of the buffer.|
### mgos_soft_uart_read_mbuf
```c
size_t mgos_soft_uart_read_mbuf(int uart_no, struct mbuf *mb, size_t len);
```
Like [mgos_soft_uart_read()](#mgos_soft_uart_read), but reads into an mbuf.

|Parameter||
|--|--|
|uart_no|UART number.|
|buf|`mbuf` pointer.|
|len|Len of the buffer.|
### mgos_soft_uart_read_avail
```c
size_t mgos_soft_uart_read_avail(int uart_no);
```
Returns the number of bytes available for reading.

|Parameter||
|--|--|
|uart_no|UART number.|
### mgos_soft_uart_set_rx_enabled
```c
bool mgos_soft_uart_set_rx_enabled(int uart_no, bool enabled);
```
Controls whether UART receiver is enabled. Returns `false` if error.

|Parameter||
|--|--|
|uart_no|UART number.|
|enabled|`true` to enable Rx.|
### mgos_soft_uart_is_rx_enabled
```c
bool mgos_soft_uart_is_rx_enabled(int uart_no);
```
Returns whether UART receiver is enabled.

|Parameter||
|--|--|
|uart_no|UART number.|
### mgos_soft_uart_write
```c
size_t mgos_soft_uart_write(int uart_no, const void *buf, size_t len);
```
Write data to the UART. Note: if there is enough space in the output buffer, the call will return immediately, otherwise it will wait for buffer to drain. If you want the call to not block, check [mgos_soft_uart_write_avail()](#mgos_soft_uart_write_avail) first.

|Parameter||
|--|--|
|uart_no|UART number.|
|buf|Buffer.|
|len|Len of the buffer.|
### mgos_soft_uart_write_avail
```c
size_t mgos_soft_uart_write_avail(int uart_no);
```
Returns amount of space availabe in the output buffer.

|Parameter||
|--|--|
|uart_no|UART number.|
### mgos_soft_uart_printf
```c
int mgos_soft_uart_printf(int uart_no, const char *fmt, ...);
```
Write data to UART, printf style. Note: currently this requires that data is fully rendered in memory before sending. There is no fixed limit as heap allocation is used, but be careful when printing longer strings.

|Parameter||
|--|--|
|uart_no|UART number.|
|fmt|String format.|
|...|String format args.|
### mgos_soft_uart_flush
```c
void mgos_soft_uart_flush(int uart_no);
```
Flush the UART output buffer. Waits for data to be sent.

|Parameter||
|--|--|
|uart_no|UART number.|
## To Do
- Implement javascript APIs for [Mongoose OS MJS](https://github.com/mongoose-os-libs/mjs).
- Test the library on ESP32 and other MCUs.