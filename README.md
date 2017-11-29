# node-pcsclite
[![Gitter](https://badges.gitter.im/Join Chat.svg)](https://gitter.im/santigimeno/node-pcsclite?utm_source=badge&utm_medium=badge&utm_campaign=pr-badge&utm_content=badge)

Bindings over pcsclite to access Smart Cards. Starting with version **0.4.0** it works in **Linux**, **OS X** and **Windows**.

## Installation

You'll need a node version installed in the system. You have several options:

* Download and install it from the [node website](http://nodejs.org/)
* Install it from your own distro. For the Debian users:

    `apt-get install nodejs nodejs-legacy`

* Use [nodesource repository](https://github.com/nodesource/distributions) for different distros.
* Use tools like [nave](https://github.com/isaacs/nave) or [nvm](https://github.com/creationix/nvm)

In order to install the package you need to have installed in the system the
pcsclite libraries. For example, in Debian/Ubuntu:

    apt-get install libpcsclite1 libpcsclite-dev

Once they are installed just run:

    npm install pcsclite

To run any code you will also need to have installed the pcsc daemon:

    apt-get install pcscd

## Example

```js
var pcsc = require('pcsclite');

var pcsc = pcsc();
pcsc.on('reader', function(reader) {

    console.log('New reader detected', reader.name);

    reader.on('error', function(err) {
        console.log('Error(', this.name, '):', err.message);
    });

    reader.on('status', function(status) {
        console.log('Status(', this.name, '):', status);
        /* check what has changed */
        var changes = this.state ^ status.state;
        if (changes) {
            if ((changes & this.SCARD_STATE_EMPTY) && (status.state & this.SCARD_STATE_EMPTY)) {
                console.log("card removed");/* card removed */
                reader.disconnect(reader.SCARD_LEAVE_CARD, function(err) {
                    if (err) {
                        console.log(err);
                    } else {
                        console.log('Disconnected');
                    }
                });
            } else if ((changes & this.SCARD_STATE_PRESENT) && (status.state & this.SCARD_STATE_PRESENT)) {
                console.log("card inserted");/* card inserted */
                reader.connect({ share_mode : this.SCARD_SHARE_SHARED }, function(err, protocol) {
                    if (err) {
                        console.log(err);
                    } else {
                        console.log('Protocol(', reader.name, '):', protocol);
                        reader.transmit(new Buffer([0x00, 0xB0, 0x00, 0x00, 0x20]), 40, protocol, function(err, data) {
                            if (err) {
                                console.log(err);
                            } else {
                                console.log('Data received', data);
                                reader.close();
                                pcsc.close();
                            }
                        });
                    }
                });
            }
        }
    });

    reader.on('end', function() {
        console.log('Reader',  this.name, 'removed');
    });
});

pcsc.on('error', function(err) {
    console.log('PCSC error', err.message);
});
```

## API

### Class: PCSCLite

The PCSCLite object is an EventEmitter that notifies the existence of Card Readers.

#### Event:  'error'

* *err* `Error Object`. The error.

#### Event:  'reader'

* *reader* `CardReader`. A CardReader object associated to the card reader detected

Emitted whenever a new card reader is detected.

#### pcsclite.close()

It frees the resources associated with this PCSCLite instance. At a low level it calls [`SCardCancel`](http://pcsclite.alioth.debian.org/pcsc-lite/node21.html) so it stops watching for new readers.

#### pcsclite.readers

An object containing all detected readers by name. Updated as readers are attached and removed.

### Class: CardReader

The CardReader object is an EventEmitter that allows to manipulate a card reader.

#### Event:  'error'

* *err* `Error Object`. The error.

#### Event:  'end'

Emitted when the card reader has been removed.

#### Event:  'status'

* *status* `Object`.
    * *state* The current status of the card reader as returned by [`SCardGetStatusChange`](http://pcsclite.alioth.debian.org/pcsc-lite/node20.html)
    * *atr* ATR of the card inserted (if any)

Emitted whenever the status of the reader changes.

#### reader.connect([options], callback)

* *options* `Object` Optional
    * *share_mode* `Number` Shared mode. Defaults to `SCARD_SHARE_EXCLUSIVE`
    * *protocol* `Number` Preferred protocol. Defaults to `SCARD_PROTOCOL_T0 | SCARD_PROTOCOL_T1`
* *callback* `Function` called when connection operation ends
    * *error* `Error`
    * *protocol* `Number` Established protocol to this connection.

Wrapper around [`SCardConnect`](http://pcsclite.alioth.debian.org/pcsc-lite/node12.html). Establishes a connection to the reader.

#### reader.disconnect(disposition, callback)

* *disposition* `Number`. Reader function to execute. Defaults to `SCARD_UNPOWER_CARD`
* *callback* `Function` called when disconnection operation ends
    * *error* `Error`

Wrapper around [`SCardDisconnect`](http://pcsclite.alioth.debian.org/pcsc-lite/node14.html). Terminates a connection to the reader.

#### reader.transmit(input, res_len, protocol, callback)

* *input* `Buffer` input data to be transmitted
* *res_len* `Number`. Max. expected length of the response
* *protocol* `Number`. Protocol to be used in the transmission
* *callback* `Function` called when transmit operation ends
    * *error* `Error`
    * *output* `Buffer`

Wrapper around [`SCardTransmit`](http://pcsclite.alioth.debian.org/pcsc-lite/node17.html). Sends an APDU to the smart card contained in the reader connected to.

#### reader.control(input, control_code, res_len, callback)

* *input* `Buffer` input data to be transmitted
* *control_code* `Number`. Control code for the operation
* *res_len* `Number`. Max. expected length of the response
* *callback* `Function` called when control operation ends
    * *error* `Error`
    * *output* `Buffer`

Wrapper around [`SCardControl`](http://pcsclite.alioth.debian.org/pcsc-lite/node18.html). Sends a command directly to the IFD Handler (reader driver) to be processed by the reader.

#### reader.close()

It frees the resources associated with this CardReader instance. At a low level it calls [`SCardCancel`](http://pcsclite.alioth.debian.org/pcsc-lite/node21.html) so it stops watching for the reader status changes.
