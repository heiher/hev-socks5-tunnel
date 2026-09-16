package hev.htproxy;

/**
 * JNI binding shipped inside hev-socks5-tunnel.aar.
 *
 * src/hev-jni.c registers its natives to this exact class when the library is
 * loaded (JNI_OnLoad -> FindClass(PKGNAME "/" CLSNAME) + RegisterNatives), with
 * PKGNAME/CLSNAME defaulting to hev/htproxy + TProxyService. Renaming or
 * repackaging this class therefore requires rebuilding the library with other
 * PKGNAME/CLSNAME values (see Application.mk) instead of editing this file.
 */
public final class TProxyService {
    private TProxyService() {
    }

    static {
        System.loadLibrary("hev-socks5-tunnel");
    }

    public static native boolean TProxyStartService(String config_path, int fd);

    public static native boolean TProxyStopService();

    public static native boolean TProxyIsRunning();

    public static native long[] TProxyGetStats();
}
