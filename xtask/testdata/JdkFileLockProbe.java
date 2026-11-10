import java.nio.ByteBuffer;
import java.nio.channels.FileChannel;
import java.nio.channels.FileLock;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.StandardOpenOption;
public final class JdkFileLockProbe {
    public static void main(String[] args) throws Exception {
        Path data=Path.of("/tmp/thos-jdk-lock.data"), ready=Path.of("/tmp/thos-jdk-lock.ready"), go=Path.of("/tmp/thos-jdk-lock.go");
        try (FileChannel file=FileChannel.open(data, StandardOpenOption.READ, StandardOpenOption.WRITE, StandardOpenOption.CREATE)) {
            if(args[0].equals("owner")) {
                try(FileLock lock=file.lock()) {
                    Files.writeString(ready,"ready");System.out.println("jdk-lock-owner-ready");
                    long deadline=System.nanoTime()+120_000_000_000L;
                    while(!Files.exists(go)){if(System.nanoTime()>deadline)throw new AssertionError("release signal missing");Thread.sleep(20);}
                    file.write(ByteBuffer.wrap("persisted".getBytes(java.nio.charset.StandardCharsets.US_ASCII)));file.force(true);
                }
                System.out.println("jdk-lock-owner-released");
            } else if(args[0].equals("blocked")) {
                try(FileLock lock=file.tryLock()) {if(lock!=null)throw new AssertionError("competing JVM acquired held lock");}
                System.out.println("jdk-lock-contender-blocked");
            } else {
                try(FileLock lock=file.tryLock()) {
                    if(lock==null)throw new AssertionError("released lock still blocked");
                    ByteBuffer bytes=ByteBuffer.allocate(9);if(file.read(bytes)!=9||!new String(bytes.array(),java.nio.charset.StandardCharsets.US_ASCII).equals("persisted"))throw new AssertionError("committed bytes missing");
                }
                System.out.println("jdk-lock-reopen-ok");
            }
        }
    }
}
