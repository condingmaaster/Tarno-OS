import subprocess,struct,random,json
random.seed(1731)
values=['','one two','tab\tvalue','a"b','C:\\folder\\','\\\\"',"Bob's $(literal)",'Größe 😀','\ud800','é€','line\nvalue']
cases=[]
for _ in range(1200):
 args=['C:\\Program Files\\THOS\\tool.exe']+[random.choice(values) for _ in range(random.randrange(12))]
 cases.append((subprocess.list2cmdline(args),args))
cases += [('app "left""right"',['app','left"right']),(' app',['','app']),('', [''])]
buf=bytearray()
for command,args in cases:
 data=command.encode('utf-16le','surrogatepass');buf+=struct.pack('<I',len(data)//2)+data
result=subprocess.run(['/tmp/thos-winargv16-host'],input=buf,capture_output=True,check=True).stdout;pos=0
for command,args in cases:
 n,=struct.unpack_from('<I',result,pos);pos+=4;actual=[]
 for _ in range(n):
  k,=struct.unpack_from('<I',result,pos);pos+=4;actual.append(result[pos:pos+k*2].decode('utf-16le','surrogatepass'));pos+=k*2
 assert actual==args,(repr(command),actual,args)
assert pos==len(result)
print(f'{len(cases)} UTF16 CRT parser cases PASS against independent Python list2cmdline encoding, explicit quote/leading-space cases and buffer canaries')
