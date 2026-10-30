(async()=>{
 const v=document.querySelector('video'), p=document.querySelector('#movie_player');
 if(!v || !p)return {ready:false};
 if(p.classList.contains("ad-showing"))return {ad:true};
 v.muted=true;p.mute();p.playVideo();await v.play();
 if(!document.fullscreenElement)await v.requestFullscreen();
 await new Promise(r=>setTimeout(r,2000));
 const b=v.getVideoPlaybackQuality(),t=performance.now(),begin=v.currentTime;
 const stats=p.getVideoStats();
 await new Promise(r=>setTimeout(r,10000));
 const q=v.getVideoPlaybackQuality();v.pause();
 return {title:document.title,ad:p.classList.contains("ad-showing"),duration:v.duration,elapsed:(performance.now()-t)/1000,begin,end:v.currentTime,width:v.videoWidth,height:v.videoHeight,format:stats.fmt,quality:p.getPlaybackQuality(),totalFrames:q.totalVideoFrames-b.totalVideoFrames,droppedFrames:q.droppedVideoFrames-b.droppedVideoFrames,fullscreen:!!document.fullscreenElement,paused:v.paused};
})()
