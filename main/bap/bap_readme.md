# Bitaxe Accessory Port (BAP) Protocol

## Overview

The Bitaxe Accessory Port (BAP) is a communication protocol designed for interacting with the Bitaxe. It provides a standardized way to monitor and control various aspects of the device through UART communication. BAP enables external accessories to request system information, subscribe to real-time data updates, and modify device settings.

## Protocol Specification

### Message Format

BAP messages follow a structured format similar to NMEA sentences:

```
$BAP,<COMMAND>,<PARAMETER>,<VALUE>*<CHECKSUM>\r\n
```

- **$**: Message start delimiter
- **BAP**: Protocol identifier
- **COMMAND**: Action to perform (3-4 characters)
- **PARAMETER**: Target parameter (variable length)
- **VALUE**: Associated value (optional, variable length)
- **CHECKSUM**: XOR checksum of the sentence body (2 hex characters)
- **\r\n**: Message end delimiters

### Command Types

| Command | Description | Direction |
|---------|-------------|-----------|
| `REQ` | Request a parameter value | Host → Device |
| `RES` | Response to a request or subscription update | Device → Host |
| `SUB` | Subscribe to periodic updates of a parameter | Host → Device |
| `UNSUB` | Unsubscribe from parameter updates | Host → Device |
| `SET` | Set a parameter value | Host → Device |
| `ACK` | Acknowledge successful command execution | Device → Host |
| `ERR` | Report an error condition | Device → Host |
| `CMD` | Send a command to the device | Host → Device |
| `STA` | Status update from the device | Device → Host |
| `LOG` | Log message from the device | Device → Host |

### Parameter Types

| Parameter | Description | Data Type | Example Value |
|-----------|-------------|-----------|---------------|
| `systemInfo` | Device model and ASIC information | String | "BM1366" |
| `hashrate` | Current hashrate in MH/s | Float | "20.5" |
| `temperature` | Chip temperature in Celsius | Float | "65.3" |
| `power` | Power consumption in watts | Float | "120.5" |
| `voltage` | Input voltage in volts | Float | "12.1" |
| `current` | Current draw in amps | Float | "10.2" |
| `shares` | Accepted/rejected shares | String | "123/2" |
| `frequency` | ASIC frequency in MHz | Float | "450.0" |
| `asic_voltage` | ASIC voltage in millivolts | Integer | "1200" |
| `ssid` | WiFi network SSID | String | "MyNetwork" |
| `password` | WiFi password | String | "secret123" |
| `fan_speed` | Fan speed percentage | Integer | "80" |
| `auto_fan` | Auto fan speed control | Boolean | "1" |
| `best_difficulty` | Best difficulty achieved | String | "1024" |
| `block_height` | Current network block height | Integer | "845123" |
| `wifi` | WiFi status information (ssid/password/rssi/ip) | String | Various |

## Implementation Details

### System Architecture

The BAP implementation is organized into several modules:

1. **Main Interface (`bap.c`)**
   - Initializes all BAP subsystems
   - Manages shared resources (queues, mutexes)
   - Coordinates initialization sequence

2. **Protocol Utilities (`bap_protocol.c`)**
   - Message formatting and parsing
   - Checksum calculation
   - Command/parameter string conversion

3. **UART Communication (`bap_uart.c`)**
   - UART initialization and configuration
   - Message sending and receiving
   - Communication task management

4. **Command Handlers (`bap_handlers.c`)**
   - Command parsing and dispatch
   - Implementation of specific command behaviors
   - Request and settings handling

5. **Subscription Management (`bap_subscription.c`)**
   - Subscription tracking
   - Periodic update scheduling
   - Timeout handling

### Key Features

#### Mode Awareness
BAP operates differently based on the device's WiFi connection status:
- **Connected Mode**: Full functionality available
- **AP Mode**: Limited functionality for WiFi configuration. Every five
  seconds while WiFi is not connected the host sends `CMD,mode,ap_mode`, and
  while its setup access point is up, also `RES,apSsid,<name>`

#### Subscription Management
- Clients can subscribe to real-time parameter updates
- Configurable update intervals
- Automatic timeout after 5 minutes of inactivity
- Efficient resource management with mutex protection

#### Error Handling
- Checksum validation for message integrity
- Duplicate message filtering
- Comprehensive error reporting with descriptive messages

#### Thread Safety
- Mutex protection for shared resources
- Queue-based message passing
- Task isolation for different functionalities

## Usage Examples


### Requesting System Information
```
Host:   $BAP,REQ,systemInfo*3F\r\n
Device: $BAP,RES,deviceModel,BM1366*4A\r\n
Device: $BAP,RES,asicModel,BM1366*4B\r\n
Device: $BAP,RES,pool,example.com*5C\r\n
Device: $BAP,RES,poolPort,3333*5D\r\n
Device: $BAP,RES,poolUser,worker*5E\r\n
```

### Subscribing to Hashrate Updates
```
Host:   $BAP,SUB,hashrate,1000*4A\r\n
Device: $BAP,ACK,hashrate,subscribed*5B\r\n
Device: $BAP,RES,hashrate,20.5*4C\r\n
Device: $BAP,RES,hashrate,21.0*4D\r\n
...
Host:   $BAP,UNSUB,hashrate*4E\r\n
Device: $BAP,ACK,hashrate,unsubscribed*5F\r\n
```

### Setting ASIC Frequency
```
Host:   $BAP,SET,frequency,500.0*4A\r\n
Device: $BAP,ACK,frequency,500.0*5B\r\n
```

### Setting WiFi Credentials
```
Host:   $BAP,SET,ssid,MyNetwork*4A\r\n
Device: $BAP,ACK,ssid,MyNetwork*5B\r\n
Host:   $BAP,SET,password,secret123*4C\r\n
Device: $BAP,ACK,password,password_set*5D\r\n
Device: $BAP,STA,status,restarting*5E\r\n
```

## Configuration

BAP UART pin assignments are defined per board revision in `DeviceConfig.pins.bap`:
- **Default Pins**: RX (GPIO 40), TX (GPIO 39)
- **Kconfig Enable/Disable**: Configurable via `CONFIG_ENABLE_BAP` (default `y`). Setting to `n` disables BAP.
- **Kconfig Pin Overrides**: Optionally configurable via `CONFIG_GPIO_BAP_RX` and `CONFIG_GPIO_BAP_TX` (set to `-1` to use board defaults).
- **Board Support**: If `pins.bap` is set to `NULL` for a board configuration or via `CONFIG_ENABLE_BAP=n`, BAP UART is automatically disabled.

UART settings:
- Baud rate: 115200
- Data bits: 8
- Parity: None
- Stop bits: 1

## Error Codes

| Error Code | Description |
|------------|-------------|
| `ap_mode_no_subscriptions` | Subscriptions not allowed in AP mode |
| `ap_mode_no_requests` | Requests not allowed in AP mode |
| `ap_mode_limited_settings` | Only WiFi settings allowed in AP mode |
| `missing_parameter` | Required parameter missing |
| `system_not_ready` | System not initialized |
| `invalid_range` | Parameter value outside valid range |
| `set_failed` | Failed to set parameter |
| `unsupported_parameter` | Parameter not supported |
| `subscription_timeout` | Subscription expired after 5 minutes |

## Development Guidelines

### Adding New Parameters

1. Add the parameter to the `bap_parameter_t` enum in `bap_protocol.h`
2. Add the string representation to `parameter_strings` in `bap_protocol.c`
3. Implement handling in `BAP_send_subscription_update()` if it's subscribable
4. Add handling in `BAP_handle_settings()` if it's configurable

***For a comprehensive view into adding a new parameter look into commit `e377b5c57f08c34c8b2894aceacb27180e44f52e` there was the best difficulty added.***

### Adding New Commands

1. Add the command to the `bap_command_t` enum in `bap_protocol.h`
2. Add the string representation to `command_strings` in `bap_protocol.c`
3. Register a handler in `BAP_handlers_init()` in `bap_handlers.c`
4. Implement the handler function

## Limitations

- Maximum message length: 256 characters
- Maximum 10 pending messages in send queue
- Subscription timeout: 5 minutes
- UART buffer threshold: 50% of buffer size

## Troubleshooting

### Common Issues

1. **No Response from Device**
   - Check UART wiring and pin configuration
   - Verify baud rate settings (115200)
   - Ensure proper power supply to the device

2. **Checksum Errors**
   - Verify message formatting
   - Check for data corruption on the communication line
   - Ensure proper termination characters (\r\n)

3. **Subscription Timeouts**
   - Resubscribe to parameters periodically
   - Check for communication interruptions
   - Verify device is not restarting

### Debugging Tips

- Enable verbose logging to see message flow
- Use a serial terminal to manually send commands
- Monitor UART buffer status for overflow conditions
- Check mutex acquisition failures in logs

## Bonanza 1004 serial display

Bonanza 1004 enables the existing BAP host on UART2 at 115200 baud, 8N1,
using ESP32 TX GPIO39 and RX GPIO40. The ASIC bridge uses UART1 on GPIO17/18.
Set `CONFIG_ENABLE_BAP=n` to disable the accessory, or use the existing BAP
GPIO overrides to select other pins. Keep the local LVGL display set to `NONE`;
the serial accessory runs its own display firmware.

The display can use the existing identity request and telemetry subscriptions.
The identity response keeps its existing replies and appends actual ASIC
frequency in MHz; clients that do not use that property can ignore it.
One-shot requests also support frequency, hashrate, temperature, power, fan RPM,
best difficulty and WiFi IP through the existing parameter names. These reads
return the same values and units as the corresponding subscription updates
without activating a subscription.
To receive actual ASIC frequency in MHz without polling, subscribe with
`$BAP,SUB,frequency,1000*5E\r\n`. The host sends a `RES,frequency` snapshot
on the next eligible update and pushes changed values at the requested interval,
serviced by the existing one-second subscription task. Renew the subscription
before its five-minute expiry; renewal also sends a fresh snapshot. Send
`$BAP,UNSUB,frequency*68\r\n` to stop updates. Previously supported requests
and subscriptions retain their behavior, and the existing parser, checksum
acceptance and interval handling remain unchanged.

### Dashboard fields for bonanzaDisplay

The `systemInfo` reply also carries these properties. Each is sent only once
the host has a value, so clients should keep their own fallback until then.

| Property | Value |
| --- | --- |
| `networkDifficulty` | Network difficulty, formatted like `best_difficulty` (for example `129.44T`) |
| `hostname` | The configured host name |
| `networkTarget` | The current job's network target, expanded from `nBits` |
| `bestShareHash` | Hash of the all-time best share |
| `lastShareHash` | Hash of the most recent result that met the pool or network target |

The three hash-like values are 256-bit numbers written as exactly 64 lowercase
hex digits, most significant first (the way a block explorer prints a block
hash). They are also pushed on change by existing subscriptions:
`bestShareHash` and `lastShareHash` with `best_difficulty`, and
`networkTarget` with `block_height`. Only the best difficulty is stored across
restarts, so after a restart `bestShareHash` is not sent until that stored best
is beaten.

### Per-chip lists for bonanzaDisplay's ASIC page

While `temperature` is subscribed, the host also pushes these lists on change,
for boards with one to four chips. A chip-level list has one whole number per
chip, separated by commas; a domain list gives each chip's domains separated by
`/`. A list is sent only when every chip has a value for it.

| Property | Example | Unit |
| --- | --- | --- |
| `asicTemp` | `58,57,60,59` | °C |
| `asicHashrate` | `752,748,760,741` | GH/s |
| `asicFrequency` | `800,800,800,800` | MHz, the mean of the chip's domain clocks |
| `asicVoltage` | `702,698,705,700` | mV, the sum of the chip's domain rails |
| `domainVoltage` | `351/351,349/349,353/352,350/350` | mV |
| `domainFrequency` | `800/800,800/800,800/787,800/800` | MHz |
| `domainHashrate` | `376/376,374/374` | GH/s |

On Bonanza (BZM) a domain is one of the chip's two engine stacks: its voltage
is the stack's CH0 or CH1 telemetry rail and its frequency the stack's PLL.
Temperatures and rails come from telemetry no older than two seconds. BZM
counts hashrate per chip, so it does not send `domainHashrate`; families with
more than one hash-counting domain send it only when it cannot be confused
with the rail and clock domains.

### Setup network name for bonanzaDisplay's WiFi setup screen

In AP mode the host refuses requests and subscriptions, so it pushes the name
of its setup network with every `ap_mode` message:

```
$BAP,CMD,mode,ap_mode*HH
$BAP,RES,apSsid,Bitaxe_A1B2*HH
```

The name is `Bitaxe_` and the last two bytes of the access point's MAC
address in hex, the same name the host's own screen puts in its QR code. The
access point starts at every boot and stops once WiFi connects, so both
messages are also sent for the first few seconds of a normal start; clients
should wait a little before showing a setup screen. `apSsid` is sent only
while the access point is up (`apEnabled` in the web API), so not after it is
switched off by hand. The network is open and its setup page is at
`192.168.4.1`. A client should not treat this `RES` as the host being
connected.
