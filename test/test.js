var should = require('should');
var sinon = require('sinon');
var pcsc = require('../lib/pcsclite');
var assert = require('assert');

describe('Testing PCSCLite private', function() {

    describe('#start()', function() {
        it('#start() stub', function(done) {
            var p = pcsc();
            var stub = sinon.stub(p, 'start', function(my_cb) {
                /* "MyReader\0" */
                my_cb(undefined, new Buffer("MyReader\0"));
            });

            p.on('reader', function(reader) {
                reader.name.should.equal("MyReader");
                done();
            });
        });

        it('#start() stub', function() {
            var cb = sinon.spy();
            var p = pcsc();
            var stub = sinon.stub(p, 'start', function(my_cb) {
                /* "MyReader" */
                my_cb(undefined, new Buffer("MyReader"));
            });

            p.on('reader', cb);
            process.nextTick(function () {
                sinon.assert.notCalled(cb);
            });
        });

        it('#start() stub', function() {
            var cb = sinon.spy();
            var p = pcsc();
            var stub = sinon.stub(p, 'start', function(my_cb) {
                /* "MyReader1\0MyReader2\0" */
                my_cb(undefined, new Buffer("MyReader1\0MyReader2\0"));
            });

            p.on('reader', cb);
            process.nextTick(function () {
                sinon.assert.calledTwice(cb);
                assert(cb.args[0][0]['name'], "MyReader1");
                assert(cb.args[1][0]['name'], "MyReader2");
            });
        });
    });
});

describe('Testing CardReader private', function() {

    var get_reader = function() {
        var p = pcsc();
        var stub = sinon.stub(p, 'start', function(my_cb) {
            /* "MyReader\0" */
            my_cb(undefined, new Buffer("MyReader\0"));
        });

        return p;
    };

    describe('#_connect()', function() {

        it('#_connect() success', function(done) {
            var p = get_reader();
            p.on('reader', function(reader) {
                var connect_stub = sinon.stub(reader, '_connect', function(connect_cb) {
                    connect_cb(undefined, 1);
                });

                reader.connect(function(err, protocol) {
                    should.not.exist(err);
                    protocol.should.equal(1);
                    done();
                });
            });
        });

        it('#_connect() error', function() {
            var p = get_reader();
            p.on('reader', function(reader) {
                var cb = sinon.spy();
                var connect_stub = sinon.stub(reader, '_connect', function(connect_cb) {
                    connect_cb("");
                });

                reader.connect(cb);
                sinon.assert.calledOnce(cb);
            });
        });

        it('#_connect() already connected', function() {
            var p = get_reader();
            p.on('reader', function(reader) {
                var cb = sinon.spy();
                reader.connected = true;

                reader.connect(cb);
                process.nextTick(function () {
                    sinon.assert.calledOnce(cb);
                });
            });
        });

    });

    describe('#_disconnect()', function() {

        it('#_disconnect() success', function() {
            var p = get_reader();
            p.on('reader', function(reader) {
                reader.connected = true;
                var cb = sinon.spy();
                var connect_stub = sinon.stub(reader, '_disconnect', function(disconnect_cb) {
                    disconnect_cb(undefined);
                });

                reader.disconnect(cb);
                sinon.assert.calledOnce(cb);
            });
        });

        it('#_disconnect() error', function() {
            var p = get_reader();
            p.on('reader', function(reader) {
                reader.connected = true;
                var cb = sinon.spy();
                var connect_stub = sinon.stub(reader, '_disconnect', function(disconnect_cb) {
                    disconnect_cb("");
                });

                reader.disconnect(cb);
                sinon.assert.calledOnce(cb);
            });
        });

        it('#_disconnect() already disconnected', function() {
            var p = get_reader();
            p.on('reader', function(reader) {
                var cb = sinon.spy();
                var connect_stub = sinon.stub(reader, '_disconnect', function(disconnect_cb) {
                    disconnect_cb(undefined);
                });

                reader.disconnect(cb);
                process.nextTick(function () {
                    sinon.assert.calledOnce(cb);
                });
            });
        });
    });
});
