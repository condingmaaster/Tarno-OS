/usr/lib/jvm/java-21-openjdk-amd64/bin/java -Xshare:off -XX:TieredStopAtLevel=1 -XX:+UseSerialGC -Xmx96m -XX:ReservedCodeCacheSize=32m -XX:-UsePerfData -Xss512k -version 2>&1
/usr/lib/jvm/java-21-openjdk-amd64/bin/java -Xshare:off -XX:TieredStopAtLevel=1 -XX:+UseSerialGC -Xmx96m -XX:ReservedCodeCacheSize=32m -XX:-UsePerfData -Xss512k -cp / Hello 2>&1
echo java-script-done
