static void XferCallback(SapXferCallbackInfo *pInfo)
{
   SapView *pView = (SapView *)pInfo->GetContext();

   // refresh view
   pView->Show();

   // refresh framerate
   static float lastframerate = 0.0f;

   SapTransfer* pXfer = pInfo->GetTransfer();
   if (pXfer->UpdateFrameRateStatistics())
   {
      SapXferFrameRateInfo* pFrameRateInfo = pXfer->GetFrameRateStatistics();
      float framerate = 0.0f;

      if (pFrameRateInfo->IsLiveFrameRateAvailable())
         framerate = pFrameRateInfo->GetLiveFrameRate();

      // check if frame rate is stalled
      if (pFrameRateInfo->IsLiveFrameRateStalled())
      {
         printf("Live frame rate is stalled.\n");
      }
      // update FPS only if the value changed by +/- 0.1
      else if ((framerate > 0.0f) && (abs(lastframerate - framerate) > 0.1f))
      {
         printf("Grabbing at %.1f frames/sec\n", framerate);
         lastframerate = framerate;
      }
   }
}