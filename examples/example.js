var pcsc = require('../index');

var pcsc = pcsc();
pcsc.on('reader', function(reader) {

    console.log('New reader detected', reader.name);

    reader.on('error', function(err) {
        console.log('Error(', this.name, '):', err.message);
    });

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

    reader.on('end', function() {
        console.log('Reader',  this.name, 'removed');
    });

    reader.connect(function(err, protocol) {
        if (err) {
            console.log(err);
        } else {
            console.log('Protocol(', this.name, '):', protocol);
            reader.transmit(new Buffer([0x00, 0xB0, 0x00, 0x00, 0x20]), 40, 1, function(err, data) {
                if (err) console.log(err);
                else console.log('Data received', data);
            })
        }
    });
});

pcsc.on('error', function(err) {
    console.log('PCSC error', err.message);
});
