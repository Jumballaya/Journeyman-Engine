class Inputs {

    public static keyIsPressed(key: string): boolean {
        const utf8 = String.UTF8.encode(key, true);
        const view = Uint8Array.wrap(utf8);
        return __jmKeyIsPressed(<i32>view.dataStart, view.length - 1);
    }

};