from pathlib import Path
import hashlib,json,os,re,resource,selectors,shlex,shutil,signal,subprocess,sys,time

ROOT=Path(__file__).resolve().parent
REPOSITORY='dl991107/skia-canvas-regression-20261031'
SKIA='29ed1e87a0a50f3d8347e988842d9d59e7573efa'
GN='b2afae122eeb6ce09c52d63f67dc53fc517dbdc8'
OFF='enable_tools enable_ganesh enable_graphite enable_precompile enable_optimize_size enable_spirv_validation use_gl use_metal use_vulkan use_dawn use_angle use_direct3d use_egl use_webgl use_webgpu use_partition_alloc use_perfetto use_fonthost_mac use_freetype use_fontconfig use_fontations use_harfbuzz use_icu use_icu4x use_client_icu use_bidi use_libgrapheme enable_skunicode enable_pdf enable_svg enable_skottie use_xps use_expat use_libjpeg_turbo_decode use_libjpeg_turbo_encode use_jpeg_gainmaps use_libpng_decode use_libpng_encode use_libwebp_decode use_libwebp_encode use_libavif use_crabbyavif use_libjxl_decode use_wuffs use_piex use_dng_sdk use_zlib use_rust_exif use_rust_icc use_rust_png_decode use_rust_png_for_android use_rust_png_encode use_rust_bmp_decode use_rust_ico_decode use_rust_jpeg_decode use_rust_jpeg_encode build_rust_targets compile_modules compile_sksl_tests lex'.split()
ARGS=dict(target_os='mac',target_cpu='arm64',is_official_build=True,is_debug=False,is_component_build=False,is_trivial_abi=False,skia_disable_tracing=True)
ARGS.update({'skia_'+key:False for key in OFF})
FILES={'build-check.py','real-skia-adapter.h','native/fury_flat.h','native/LICENSE.chromium','native/LICENSE.fury','native/base/containers/span.h'}|{'native/'+label+'-'+name for label in ('baseline','candidate') for name in ('buffer.cc','buffer.h','caller.cc')}

def require(value):
 if not value:raise ValueError('gate refused')
def target_values(text,key):
 value=json.loads(text);require(isinstance(value,dict) and set(value)=={'//:skia'} and isinstance(value['//:skia'],dict) and set(value['//:skia'])=={key})
 result=value['//:skia'][key];require(isinstance(result,list) and all(isinstance(x,str) and len(x)<=200 for x in result));return result
def regular(path,base,limit):
 require(path.is_relative_to(base) and all(not p.is_symlink() for p in [path,*path.parents] if p.is_relative_to(base)))
 require(path.is_file() and path.stat().st_size<=limit);return path.read_bytes()
def validate():
 raw=regular(ROOT/'INPUTS.json',ROOT,65536);v=json.loads(raw)
 expected=os.environ.get('EXPECTED_INPUTS_SHA256')
 require(not expected or hashlib.sha256(raw).hexdigest()==expected)
 require(set(v)=={'schema','repository','skia_revision','gn_revision','gn_args','files'})
 require(type(v['schema']) is int and v['schema']==1 and v['repository']==REPOSITORY and v['skia_revision']==SKIA and v['gn_revision']==GN and v['gn_args']==ARGS)
 require(all(type(v['gn_args'][k]) is type(value) for k,value in ARGS.items()))
 require(isinstance(v['files'],list) and len(v['files'])==len(FILES) and {e['path'] for e in v['files']}==FILES)
 for e in v['files']:
  require(set(e)=={'path','bytes','sha256'});b=regular(ROOT/e['path'],ROOT,300000)
  require(len(b)==e['bytes'] and hashlib.sha256(b).hexdigest()==e['sha256'])
 return v
def cloud_gate():
 for key,value in {'GITHUB_ACTIONS':'true','GITHUB_REPOSITORY':REPOSITORY,'GITHUB_EVENT_NAME':'workflow_dispatch','GITHUB_RUN_ATTEMPT':'1','RUNNER_OS':'macOS','RUNNER_ARCH':'ARM64'}.items():require(os.environ.get(key)==value)
 require(sys.platform=='darwin' and re.fullmatch(r'[0-9]+',os.environ.get('GITHUB_RUN_ID','')) and re.fullmatch(r'[a-f0-9]{40}',os.environ.get('GITHUB_SHA','')))
 require(bool(os.environ.get('EXPECTED_INPUTS_SHA256')))
 event=Path(os.environ['GITHUB_EVENT_PATH']);require(event.name=='event.json')
 r=json.loads(regular(event,Path(os.environ['RUNNER_TEMP']),1048576))['repository']
 require(r['full_name']==REPOSITORY and r['private'] is False and r['visibility']=='public' and r['owner']['login']=='dl991107' and r['owner']['id']==309128050)

class Bounded:
 def __init__(self,path,seconds=4200):
  self.path=path;path.mkdir(mode=0o700);self.work=path/'work';self.work.mkdir(mode=0o700)
  self.log=(path/'run.log').open('xb');self.end=time.monotonic()+seconds;self.initial=shutil.disk_usage(path).free;self.low=self.initial;self.phases=[]
  self.env={k:os.environ[k] for k in ['PATH','DEVELOPER_DIR','LANG'] if k in os.environ}
  self.env.update(HOME=str(self.work),CFFIXED_USER_HOME=str(self.work),TMPDIR=str(self.work),GIT_CONFIG_GLOBAL='/dev/null',GIT_CONFIG_NOSYSTEM='1',GIT_TERMINAL_PROMPT='0',DEPOT_TOOLS_UPDATE='0',ASAN_OPTIONS='detect_leaks=0:halt_on_error=1',UBSAN_OPTIONS='halt_on_error=1:print_stacktrace=1')
 def guard(self):
  free=shutil.disk_usage(self.path).free;self.low=min(self.low,free)
  require(free>=10*2**30 and self.initial-free<=25*2**30)
  if time.monotonic()>=self.end:raise TimeoutError()
 def command(self,name,args,cwd=None,allowed=(0,),extra=None):
  self.guard();start=time.monotonic();env=dict(self.env);env.update(extra or {});capture=bytearray();status=None
  p=subprocess.Popen(args,cwd=cwd or self.work,env=env,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,start_new_session=True)
  sel=selectors.DefaultSelector();sel.register(p.stdout,selectors.EVENT_READ)
  try:
   while sel.get_map() or p.poll() is None:
    self.guard()
    for key,_ in sel.select(.2):
     data=os.read(key.fd,16384)
     if not data:sel.unregister(key.fd);continue
     require(self.log.tell()+len(data)<=8*2**20);self.log.write(data);self.log.flush();capture.extend(data)
   status=p.wait(timeout=max(.01,self.end-time.monotonic()));self.guard()
  finally:
   sel.close();p.stdout.close()
   try:os.killpg(p.pid,signal.SIGTERM)
   except ProcessLookupError:pass
   try:p.wait(timeout=1)
   except subprocess.TimeoutExpired:pass
   try:os.killpg(p.pid,signal.SIGKILL)
   except ProcessLookupError:pass
   p.wait(timeout=2);self.phases.append({'name':name,'exit_code':status,'seconds':round(time.monotonic()-start,3)})
  require(status in allowed);return capture.decode('utf-8',errors='replace').strip()
 def repo(self,name,url,revision):
  d=self.work/name;d.mkdir();g=['/usr/bin/git','-c','core.hooksPath=/dev/null','-C',str(d)]
  for label,args in [('init',['init','-q']),('fetch',['fetch','--depth=1',url,revision]),('checkout',['checkout','-q','--detach','FETCH_HEAD'])]:self.command(name+'-'+label,g+args)
  require(self.command(name+'-pin',g+['rev-parse','HEAD'])==revision)
  require(not self.command(name+'-clean',g+['status','--porcelain','--untracked-files=no']));return d

def generate(label,d):
 source=(ROOT/'native'/(label+'-buffer.cc')).read_text();m=re.search(r'(?:void|bool) ImageDataBuffer::ApplyFuryNoise\(\)\s*\{',source);require(m is not None)
 start=m.start();end=source.index('{',start)+1;depth=1
 while depth:depth+=(source[end]=='{')-(source[end]=='}');end+=1
 method=source[start:end];header=(ROOT/'native'/(label+'-buffer.h')).read_text();decl=re.search(r'(?:void|bool) ApplyFuryNoise\(\);',header)[0]
 html=(ROOT/'native'/(label+'-caller.cc')).read_text();html=html[html.index('String HTMLCanvasElement::ToDataURLInternal('):]
 caller=html[html.index('    if (FuryWantsCanvasNoise()'):html.index('    String data_url')]
 d.mkdir();(d/'declaration.inc').write_text(decl+'\n')
 unit=d/'test.cc';unit.write_text('#include "real-skia-adapter.h"\nnamespace blink {\n'+method+'\nString Export(ImageDataBuffer* data_buffer) {\n'+caller+'\nreturn "encoded";\n}\n}\n')
 return unit,{'method_sha256':hashlib.sha256(method.encode()).hexdigest(),'caller_sha256':hashlib.sha256(caller.encode()).hexdigest()}

def execute(b,result):
 ninja=shutil.which('ninja',path=b.env['PATH']);require(ninja and Path(ninja).is_file())
 versions={'ninja':b.command('ninja-version',[ninja,'--version'])}
 cc=b.command('clang-path',['/usr/bin/xcrun','--find','clang']);cxx=b.command('clangxx-path',['/usr/bin/xcrun','--find','clang++']);ar=b.command('ar-path',['/usr/bin/xcrun','--find','ar'])
 sdk=b.command('sdk-path',['/usr/bin/xcrun','--sdk','macosx','--show-sdk-path'])
 for tool in [cc,cxx,ar]:require(Path(tool).is_file() and Path(tool).is_absolute())
 require(Path(sdk).is_dir() and sdk.endswith('.sdk'))
 versions.update(compiler=b.command('compiler-version',[cxx,'--version']),sdk=b.command('sdk-version',['/usr/bin/xcrun','--sdk','macosx','--show-sdk-version']))
 gnroot=b.repo('gn','https://gn.googlesource.com/gn',GN);gnout=gnroot/'out';gnout.mkdir()
 (gnout/'last_commit_position.h').write_text('#define LAST_COMMIT_POSITION_NUM 0\n#define LAST_COMMIT_POSITION "local-source-build '+GN+'"\n')
 b.command('gn-generate',[sys.executable,'build/gen.py','--no-last-commit-position','--allow-warnings'],gnroot,extra={'CXX':shlex.quote(cxx),'AR':shlex.quote(ar)})
 b.command('gn-build',[ninja,'-C',str(gnout),'-j1','gn']);gn=gnout/'gn';versions['gn']=b.command('gn-version',[str(gn),'--version'])
 skia=b.repo('skia','https://skia.googlesource.com/skia.git',SKIA);out=skia/'out/cpu';out.mkdir(parents=True)
 args=dict(ARGS,cc=cc,cxx=cxx,xcode_sysroot=sdk)
 (out/'args.gn').write_text('\n'.join(k+' = '+json.dumps(v) for k,v in sorted(args.items()))+'\n')
 b.command('skia-generate',[str(gn),'gen',str(out),'--fail-on-unused-args'],skia)
 deps=target_values(b.command('skia-deps',[str(gn),'desc',str(out),'//:skia','deps','--all','--format=json'],skia),'deps');require(not any('//third_party/externals/' in x for x in deps))
 defines=target_values(b.command('skia-defines',[str(gn),'desc',str(out),'//:skia','defines','--format=json'],skia),'defines')
 b.command('skia-build',[ninja,'-C',str(out),'-j1','skia']);library=out/'libskia.a';require(library.is_file())
 result.update(versions=versions,gn_sha256=hashlib.sha256(gn.read_bytes()).hexdigest(),ninja_sha256=hashlib.sha256(Path(ninja).read_bytes()).hexdigest(),library_sha256=hashlib.sha256(library.read_bytes()).hexdigest(),skia_revision=SKIA,gn_revision=GN,args=args,defines=defines,dependency_targets=deps,cpu_only=True,full_chromium=False,runs={})
 for label in ['baseline','candidate']:
  unit,meta=generate(label,b.work/label);binary=unit.parent/'test'
  command=[cxx,'-std=c++20','-O1','-g','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-fno-omit-frame-pointer','-I'+str(ROOT),'-I'+str(ROOT/'native'),'-I'+str(skia),'-I'+str(unit.parent)]+['-D'+x for x in defines]+[str(unit),str(library),'-framework','ApplicationServices','-framework','OpenGL','-o',str(binary)]
  b.command(label+'-compile',command);text=b.command(label+'-run',[str(binary)],allowed=(1,) if label=='baseline' else (0,))
  result['runs'][label]=dict(meta,output=text);result['real_skia_ran']=True
  expected='literal_pixel 1,1,1,1' if label=='baseline' else 'literal_pixel 255,255,255,1'
  require(expected in text and ('PASS real_skia_cpu=1 reference_roundtrips=32895 formats=2 seeds=2 injected_failures=2' in text if label=='candidate' else 'FAIL line ' in text))

def main():
 b=None;report={'status':'rejected','real_skia_ran':False};code=2
 try:
  require(sys.argv[1:] in [['--check'],['--run']]);validate()
  if sys.argv[1]=='--check':report['status']='validated';code=0
  else:
   cloud_gate();b=Bounded(ROOT/'evidence')
   for sig in [signal.SIGTERM,signal.SIGINT]:signal.signal(sig,lambda *_:(_ for _ in ()).throw(TimeoutError()))
   report.update(status='failed');execute(b,report);report['status']='passed';code=0
 except Exception as e:report['error_type']=type(e).__name__
 finally:
  if b:
   report.update(phases=b.phases,max_child_rss_bytes=resource.getrusage(resource.RUSAGE_CHILDREN).ru_maxrss,min_free_bytes=b.low)
   b.log.close();(b.path/'report.json').write_text(json.dumps(report,indent=2)+'\n')
 print(json.dumps(report));return code
if __name__=='__main__':raise SystemExit(main())
