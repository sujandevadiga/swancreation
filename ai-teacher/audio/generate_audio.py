from pathlib import Path
import subprocess, json, csv, hashlib, re
from concurrent.futures import ThreadPoolExecutor

root=Path(__file__).resolve().parent
import tempfile
temporary = tempfile.TemporaryDirectory(prefix='ai-teacher-tts-')
work=Path(temporary.name)
(root/'data').mkdir(exist_ok=True)
correct=[
'Correct! Well done.',
'That is right! Great thinking.',
'Excellent work! You got it.',
'Good job! That is the correct answer.',
'Well done! Keep learning.',
'Yes! You chose the right answer.',
'Great work! Keep going.',
'Correct! Your practice is paying off.',
'Nicely done! That is right.',
'You got it! Keep up the good work.',
'That is correct! Ready for more learning?',
'Well answered! Keep thinking carefully.',
'Great choice! That answer is correct.',
'Correct! You are making progress.',
'Good thinking! You found the answer.',
'Yes, that is right! Keep exploring.',
'Excellent! Another correct answer.',
'Well done! You understood this one.',
'That is right! Keep building your knowledge.',
'Correct! Every step helps you learn.'
]
wrong=[
'Not quite. Let us learn from this one.',
'That was not the correct answer. Keep learning.',
'Good effort. Read the explanation and keep going.',
'Not this time. Mistakes help us learn.',
'Take your time. You can learn this.',
'That answer was not quite right. Keep practicing.',
'Keep going. Look at the correct answer.',
'Good try. Let us understand the explanation.',
'Not quite. Think about what you learned.',
'It is okay to make mistakes. Keep trying.',
'Let us learn the right answer together.',
'Good effort. Take a moment to understand this one.',
'That was a tricky one. Keep learning.',
'Not quite right. Review the answer on the screen.',
'Keep practicing. Every question teaches you something.',
'Let us check the explanation. You are still learning.',
'Good try. Keep thinking carefully.',
'Not this time. Take a breath and keep going.',
'Look at the explanation. It will help you learn.',
'One mistake does not stop your progress. Keep learning.'
]
clips=[('welcome.mp3','Welcome to AI Teacher. Choose your class and subject. Let us learn together.')]
clips += [(f'c{i:02d}.mp3',text) for i,text in enumerate(correct)]
clips += [(f'w{i:02d}.mp3',text) for i,text in enumerate(wrong)]
assert len(clips)==41

def build(item):
 name,text=item
 textfile=work/(Path(name).stem+'.txt');textfile.write_text(text)
 output=root/'data'/name
 cmd=['ffmpeg','-nostdin','-hide_banner','-loglevel','error','-f','lavfi','-i',f'flite=textfile={textfile}:voice=slt','-af','loudnorm=I=-18:TP=-2:LRA=7','-ar','22050','-ac','1','-c:a','libmp3lame','-b:a','48k','-threads','1','-write_xing','0','-id3v2_version','0','-y',str(output)]
 subprocess.run(cmd,check=True,capture_output=True)
 info=json.loads(subprocess.check_output(['ffprobe','-v','error','-show_entries','format=duration,size:stream=codec_name,sample_rate,channels','-of','json',str(output)]))
 duration=float(info['format']['duration']);stream=info['streams'][0]
 assert 0.5<duration<7.5,(name,duration)
 assert stream['codec_name']=='mp3' and stream['channels']==1 and stream['sample_rate']=='22050',name
 decoded=subprocess.run(['ffmpeg','-nostdin','-hide_banner','-v','info','-i',str(output),'-af','volumedetect','-f','null','-'],capture_output=True,text=True,check=True)
 mean=re.search(r'mean_volume:\s*(-?[0-9.]+) dB',decoded.stderr)
 assert mean and float(mean[1])>-50,(name,'silent or very quiet')
 return {'filename':name,'text':text,'seconds':round(duration,3),'bytes':output.stat().st_size,'mean_db':float(mean[1]),'sha256':hashlib.sha256(output.read_bytes()).hexdigest()}

with ThreadPoolExecutor(max_workers=2) as executor:results=list(executor.map(build,clips))
(root/'manifest.json').write_text(json.dumps({'voice':'CMU Flite slt, synthetic English','format':'MP3, mono, 22050 Hz, 48 kbit/s','clips':results},indent=2)+'\n')
with (root/'transcripts.csv').open('w',newline='') as f:
 w=csv.writer(f);w.writerow(['filename','spoken_text']);w.writerows((r['filename'],r['text']) for r in results)
(root/'SHA256SUMS.txt').write_text(''.join(r['sha256']+'  data/'+r['filename']+'\n' for r in results))
print('Generated and fully decoded',len(results),'MP3 clips')
print('Total bytes:',sum(r['bytes'] for r in results))
print('Longest clip:',max(r['seconds'] for r in results),'seconds')
