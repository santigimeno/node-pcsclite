var events = require('events');
require('buffertools');

/* Make sure we choose the correct build directory */
var bindings = require('bindings')('pcsclite');
var PCSCLite = bindings.PCSCLite;
var CardReader = bindings.CardReader;
inherits(PCSCLite, events.EventEmitter);
inherits(CardReader, events.EventEmitter);

module.exports = function() {

    var readers = [];
    var p = new PCSCLite();
    process.nextTick(function() {
        p.start(function(e, data) {
            if (e) {
                p.emit('error', e);
            } else {
                /* parse data buffer to get the card reader name, and get the reader */
                var readers_aux = [];
                var ini = 0;
                var pos = 0;
                while((pos = data.slice(ini).indexOf('\0')) > 0) {
                    var name = data.slice(ini, ini + pos).toString();
                    var is_old = false;
                    for (var i = 0; i < readers.length; ++i) {
                        if (readers[i].name === name) {
                            readers_aux.push(readers[i]);
                            is_old = true;
                            break;
                        }
                    }

                    if (!is_old) {
                        var r = new CardReader(name);
                        readers_aux.push(r);
                        p.emit('reader', r);
                        r.get_status(function(e, state, atr) {
                            if (e) {
                                r.emit('error', e);
                            } else {
                                var status = { state : state };
                                if (atr) {
                                    status.atr = atr;
                                }

                                r.emit('status', status);
                                r.state = state;
                            }
                        });
                    }

                    ini += pos + 1;
                }

                readers = readers_aux;
            }
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
