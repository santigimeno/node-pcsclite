node-pcsclite
=============

Bindings over pcsclite to access Smart Cards

Installation
============

In order to install the package you need to have installed in the system the
pcsclite libraries. In Debian/Ubuntu:

    apt-get install libpcsclite1 libpcsclite-dev

Once they are installed just run:

    npm install pcsclite

To run any code you will also need to have installed the pcsc daemon:

    apt-get install pcscd

Example
=======

    var pcsc = require('pcsclite');

    var pcsc = pcsc();
    /* Check for new card reader detection */
    pcsc.on('reader', function(reader) {
        console.log('New reader detected', reader.name);

        /* Check for reader status changes such a new card insertion */
        reader.on('status', function(status) {
            console.log('Status(', this.name, '):', status);
            /* check what has changed */
            var changes = this.status ^ status;
            if (changes) {
                if ((changes & this.SCARD_STATE_EMPTY) && (status & this.SCARD_STATE_EMPTY)) {
                    console.log("card removed");/* card removed */
                } else if ((changes & this.SCARD_STATE_PRESENT) && (status & this.SCARD_STATE_PRESENT)) {
                    console.log("card inserted");/* card inserted */
                }
            }
        });

        /* You can connect to a smart card */
        reader.connect(function(err, protocol) {
            if (err) {
                console.log(err);
            } else {
                console.log('Protocol(', this.name, '):', protocol);
                /* And transmit data */
                reader.transmit(new Buffer([0x00, 0xB0, 0x00, 0x00, 0x20]), 40, 1, function(err, data) {
                    if (err) console.log(err);
                    else console.log('Data received', data);
                })
            }
        });

        reader.on('end', function() {
            console.log('Reader',  this.name, 'removed');
        });

        reader.on('error', function(err) {
            console.log('Error(', this.name, '):', err.message);
        });
    });

    pcsc.on('error', function(err) {
        console.log('PCSC error', err.message);
    });

