var LCUI = globalThis.LCUI = globalThis.LCUI || {};

LCUI.Zip = (function () {
    'use strict';

    var table = null;

    function crcTable() {
        if (table) return table;
        table = new Uint32Array(256);
        for (var i = 0; i < 256; ++i) {
            var value = i;
            for (var bit = 0; bit < 8; ++bit)
                value = value & 1 ? 0xedb88320 ^ (value >>> 1) : value >>> 1;
            table[i] = value >>> 0;
        }
        return table;
    }

    function crc32(bytes) {
        var lookup = crcTable();
        var value = 0xffffffff;
        for (var i = 0; i < bytes.length; ++i)
            value = lookup[(value ^ bytes[i]) & 0xff] ^ (value >>> 8);
        return (value ^ 0xffffffff) >>> 0;
    }

    function encode(text) {
        if (globalThis.TextEncoder) return new TextEncoder().encode(text);
        var value = unescape(encodeURIComponent(text));
        var bytes = new Uint8Array(value.length);
        for (var i = 0; i < value.length; ++i) bytes[i] = value.charCodeAt(i);
        return bytes;
    }

    function push16(list, value) {
        list.push(value & 0xff, (value >>> 8) & 0xff);
    }

    function push32(list, value) {
        list.push(value & 0xff, (value >>> 8) & 0xff, (value >>> 16) & 0xff, (value >>> 24) & 0xff);
    }

    function build(files) {
        var local = [];
        var central = [];
        var offset = 0;
        var time = 0;
        var date = 0x21;

        files.forEach(function (entry) {
            var name = encode(entry.name);
            var data = encode(String(entry.content === undefined ? '' : entry.content));
            var checksum = crc32(data);

            push32(local, 0x04034b50);
            push16(local, 20);
            push16(local, 0x0800);
            push16(local, 0);
            push16(local, time);
            push16(local, date);
            push32(local, checksum);
            push32(local, data.length);
            push32(local, data.length);
            push16(local, name.length);
            push16(local, 0);
            name.forEach(function (byte) { local.push(byte); });
            data.forEach(function (byte) { local.push(byte); });

            push32(central, 0x02014b50);
            push16(central, 20);
            push16(central, 20);
            push16(central, 0x0800);
            push16(central, 0);
            push16(central, time);
            push16(central, date);
            push32(central, checksum);
            push32(central, data.length);
            push32(central, data.length);
            push16(central, name.length);
            push16(central, 0);
            push16(central, 0);
            push16(central, 0);
            push16(central, 0);
            push32(central, 0);
            push32(central, offset);
            name.forEach(function (byte) { central.push(byte); });

            offset += 30 + name.length + data.length;
        });

        var end = [];
        push32(end, 0x06054b50);
        push16(end, 0);
        push16(end, 0);
        push16(end, files.length);
        push16(end, files.length);
        push32(end, central.length);
        push32(end, offset);
        push16(end, 0);

        return new Blob([new Uint8Array(local), new Uint8Array(central), new Uint8Array(end)], { type: 'application/zip' });
    }

    function download(files, name) {
        var blob = build(files);
        var link = document.createElement('a');
        link.href = URL.createObjectURL(blob);
        link.download = name || 'lcui-project.zip';
        link.click();
        setTimeout(function () { URL.revokeObjectURL(link.href); }, 4000);
    }

    return { build: build, download: download };
})();
