/usr/lib/jvm/java-21-openjdk-amd64/bin/java -Xshare:off -XX:TieredStopAtLevel=1 -XX:+UseSerialGC -Xmx96m -XX:ReservedCodeCacheSize=32m -XX:-UsePerfData -Xss512k -version 2>&1
/usr/lib/jvm/java-21-openjdk-amd64/bin/java -Xshare:off -XX:TieredStopAtLevel=1 -XX:+UseSerialGC -Xmx96m -XX:ReservedCodeCacheSize=32m -XX:-UsePerfData -Xss512k -cp / Hello 2>&1
cat > /tmp/CompileProbe.java <<'EOF'
import java.util.stream.IntStream;
public class CompileProbe {
    public static void main(String[] args) {
        System.out.println("compiler-says-" + IntStream.rangeClosed(1, 10).sum());
    }
}
EOF
/usr/lib/jvm/java-21-openjdk-amd64/bin/javac -J-Xshare:off -J-XX:TieredStopAtLevel=1 -J-XX:+UseSerialGC -J-Xmx96m -J-XX:ReservedCodeCacheSize=32m /tmp/CompileProbe.java 2>&1
/usr/lib/jvm/java-21-openjdk-amd64/bin/java -Xshare:off -XX:TieredStopAtLevel=1 -XX:+UseSerialGC -Xmx96m -XX:ReservedCodeCacheSize=32m -cp /tmp CompileProbe 2>&1
echo java-script-done
