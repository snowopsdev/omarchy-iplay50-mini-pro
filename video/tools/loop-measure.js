(async()=>{
 const v=document.querySelector('video');
 if(!v)return {error:'video missing'};
 const player=document.querySelector('#movie_player');
 if(!document.fullscreenElement){
   if(player)player.querySelector('.ytp-fullscreen-button').click();
   else await v.requestFullscreen();
 }
 await v.play();
 await new Promise(r=>setTimeout(r,2000));
 const q0=v.getVideoPlaybackQuality(),start=performance.now(),begin=v.currentTime;
 let previous=begin,progress=0,wraps=0;
 const timer=setInterval(()=>{
   const current=v.currentTime;let delta=current-previous;
   if(Number.isFinite(v.duration)&&delta < -v.duration/2){delta+=v.duration;wraps++;}
   if(delta>0)progress+=delta;
   previous=current;
 },100);
 await new Promise(r=>setTimeout(r,20000));clearInterval(timer);
 const q=v.getVideoPlaybackQuality();
 return {elapsed:(performance.now()-start)/1000,mediaProgress:progress,wraps,begin,end:v.currentTime,
   viewport:{width:innerWidth,height:innerHeight,dpr:devicePixelRatio},width:v.videoWidth,height:v.videoHeight,fullscreen:!!document.fullscreenElement,
   totalFrames:q.totalVideoFrames-q0.totalVideoFrames,droppedFrames:q.droppedVideoFrames-q0.droppedVideoFrames,
   ready:v.readyState,paused:v.paused,
   ad:player?player.classList.contains('ad-showing'):false,
   format:player?player.getVideoStats().fmt:null,
   error:v.error?{code:v.error.code,message:v.error.message}:null};
})()
