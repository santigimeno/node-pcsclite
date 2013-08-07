var events = require('events');
require('buffertools');

/* Make sure we choose the correct build directory */
var bindings = require('bindings')('pcsclite');
var PCSCLite = bindings.PCSCLite;
var CardReader = bindings.CardReader;
inherits(PCSCLite, events.EventEmitter);
inherits(CardReader, events.EventEmitter);

var parse_readers_string = function(readers_str) {
    var pos;
    var readers = [];
    var ini = 0;
    while ((pos = readers_str.slice(ini).indexOf('\0')) > 0) {
        readers.push(readers_str.slice(ini, ini + pos).toString());
        ini += pos + 1;
    }

    return readers;
};

module.exports = function() {

    var readers = [];
    var p = new PCSCLite();
    process.nextTick(function() {
        p.start(function(err, data) {
            if (err) {
                return p.emit('error', err);
            }

            var readers_aux = [];
            var names = parse_readers_string(data);
            var new_names = names.filter(function(name) {
                return !readers.some(function(reader) {
                    return reader.name !== name;
                });
            });

            new_names.forEach(function(name) {
                var r = new CardReader(name);
                readers_aux.push(r);
                r.get_status(function(err, state, atr) {
                    if (err) {
                        return r.emit('error', e);
                    }

                    var status = { state : state };
                    if (atr) {
                        status.atr = atr;
                    }

                    r.emit('status', status);
                    r.state = state;
                });

                p.emit('reader', r);
            });

            readers = readers_aux;
        });
    });

    return p;
};

CardReader.prototype.connect = function(cb) {

    if (!this.connected) {
        this._connect(cb);
    } else {
        cb();
    }
};

CardReader.prototype.disconnect = function(cb) {

    if (this.connected) {
        this._disconnect(cb);
    } else {
        cb();
    }
};

CardReader.prototype.transmit = function(data, res_len, protocol, cb) {

    if (!this.connected) {
        return cb(new Error("Card Reader not connected"));
    }

    this._transmit(data, res_len, protocol, cb);
};

CardReader.prototype.SCARD_STATE_UNAWARE = 0x0000;
CardReader.prototype.SCARD_STATE_IGNORE = 0x0001;
CardReader.prototype.SCARD_STATE_CHANGED = 0x0002;
CardReader.prototype.SCARD_STATE_UNKNOWN = 0x0004;
CardReader.prototype.SCARD_STATE_UNAVAILABLE = 0x0008;
CardReader.prototype.SCARD_STATE_EMPTY = 0x0010;
CardReader.prototype.SCARD_STATE_PRESENT = 0x0020;
CardReader.prototype.SCARD_STATE_ATRMATCH = 0x0040;
CardReader.prototype.SCARD_STATE_EXCLUSIVE = 0x0080;
CardReader.prototype.CARD_STATE_INUSE = 0x0100;
CardReader.prototype.SCARD_STATE_MUTE = 0x0200;

// extend prototype
function inherits(target, source) {
    for (var k in source.prototype) {
        target.prototype[k] = source.prototype[k];
    }
}
