var events = require('events');
var bt = require('buffertools');

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
    while ((pos = bt.indexOf(readers_str.slice(ini), '\0')) > 0) {
        readers.push(readers_str.slice(ini, ini + pos).toString());
        ini += pos + 1;
    }

    return readers;
};

/*
 * It returns an array with the elements contained in a that aren't contained in b
 */
function diff(a, b) {
    return a.filter(function(i) {
        return b.indexOf(i) === -1;
    });
};

module.exports = function() {

    var readers = {};
    var p = new PCSCLite();
    process.nextTick(function() {
        p.start(function(err, data) {
            if (err) {
                return p.emit('error', err);
            }

            var names = parse_readers_string(data);

            var current_names = Object.keys(readers);
            var new_names = diff(names, current_names);
            var removed_names = diff(current_names, names);

            new_names.forEach(function(name) {
                var r = new CardReader(name);
                r.on('_end', function() {
                    r.removeAllListeners('status');
                    r.emit('end');
                    delete readers[name];
                });

                readers[name] = r;
                r.get_status(function(err, state, atr) {
                    if (err) {
                        return r.emit('error', err);
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

            removed_names.forEach(function(name) {
                readers[name].close();
            });
        });
    });

    return p;
};

CardReader.prototype.connect = function(options, cb) {
    if (typeof options === 'function') {
        cb = options;
        options = undefined;
    }

    options = options || {};
    options.share_mode = options.share_mode || this.SCARD_SHARE_EXCLUSIVE;
    options.protocol = options.protocol || this.SCARD_PROTOCOL_T0 | this.SCARD_PROTOCOL_T1;

    if (!this.connected) {
        this._connect(options.share_mode, options.protocol, cb);
    } else {
        cb();
    }
};

CardReader.prototype.disconnect = function(disposition, cb) {
    if (typeof disposition === 'function') {
        cb = disposition;
        disposition = undefined;
    }

    if (typeof disposition !== 'number') {
        disposition = this.SCARD_UNPOWER_CARD;
    }

    if (this.connected) {
        this._disconnect(disposition, cb);
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

CardReader.prototype.control = function(data, control_code, res_len, cb) {
    if (!this.connected) {
        return cb(new Error("Card Reader not connected"));
    }

    var output = new Buffer(res_len);
    this._control(data, control_code, output, function(err, len) {
        if (err) {
            return cb(err);
        }

        cb(err, output.slice(0, len));
    });
};

// extend prototype
function inherits(target, source) {
    for (var k in source.prototype) {
        target.prototype[k] = source.prototype[k];
    }
}
