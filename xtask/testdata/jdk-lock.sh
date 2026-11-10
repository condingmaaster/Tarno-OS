set -e
jdk=/usr/lib/jvm/java-21-openjdk-amd64/bin
opts='-Xshare:off -XX:TieredStopAtLevel=1 -XX:+UseSerialGC -Xmx96m -XX:ReservedCodeCacheSize=32m -XX:-UsePerfData -Xss512k'
$jdk/javac -J-Xshare:off -J-XX:TieredStopAtLevel=1 -J-XX:+UseSerialGC -J-Xmx96m -J-XX:ReservedCodeCacheSize=32m -d /tmp /JdkFileLockProbe.java
/busybox rm -f /tmp/thos-jdk-lock.data /tmp/thos-jdk-lock.ready /tmp/thos-jdk-lock.go
$jdk/java $opts -cp /tmp JdkFileLockProbe owner &
owner=$!
i=0
while [ ! -f /tmp/thos-jdk-lock.ready ]; do
 /busybox sleep 1
 i=$((i+1))
 if [ "$i" -ge 90 ]; then echo jdk-lock-owner-timeout;exit 1;fi
done
$jdk/java $opts -cp /tmp JdkFileLockProbe blocked
/busybox touch /tmp/thos-jdk-lock.go
wait "$owner"
$jdk/java $opts -cp /tmp JdkFileLockProbe free
/busybox rm -f /tmp/thos-jdk-lock.data /tmp/thos-jdk-lock.ready /tmp/thos-jdk-lock.go
echo jdk-lock-done
