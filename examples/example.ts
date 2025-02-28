import PCSCLite, { CardReader } from '..';

const pcsc = PCSCLite();

const onReaderError = (e: Error) => {
    console.log(e.message);
};

const onReaderDetected = (reader: CardReader) => {
    // Listen to all other reader events here; for example,
    reader.on('error', onReaderError);
};

pcsc.on('reader', onReaderDetected);
