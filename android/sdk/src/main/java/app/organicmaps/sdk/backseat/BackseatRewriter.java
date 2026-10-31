package app.organicmaps.sdk.backseat;

import android.content.Context;
import androidx.annotation.Keep;
import androidx.annotation.NonNull;
import androidx.core.content.ContextCompat;
import app.organicmaps.sdk.util.log.Logger;
import com.google.common.util.concurrent.ListenableFuture;
import com.google.mlkit.genai.common.DownloadCallback;
import com.google.mlkit.genai.common.FeatureStatus;
import com.google.mlkit.genai.common.GenAiException;
import com.google.mlkit.genai.prompt.GenerateContentRequest;
import com.google.mlkit.genai.prompt.GenerateContentResponse;
import com.google.mlkit.genai.prompt.Generation;
import com.google.mlkit.genai.prompt.TextPart;
import com.google.mlkit.genai.prompt.java.GenerativeModelFutures;
import java.util.concurrent.Executor;

/// Rewords Backseat Driver's roasts with Gemini Nano on devices that have it (through
/// Android AICore). Everywhere else the native side just speaks its canned lines.
@Keep
public final class BackseatRewriter
{
  private static final String TAG = BackseatRewriter.class.getSimpleName();

  private static GenerativeModelFutures sModel;
  private static Executor sExecutor;

  private BackseatRewriter() {}

  /// Call once the native framework is up.
  public static void initialize(@NonNull Context context)
  {
    nativeSetAvailable(false);
    try
    {
      sExecutor = ContextCompat.getMainExecutor(context);
      sModel = GenerativeModelFutures.from(Generation.INSTANCE.getClient());
      ListenableFuture<Integer> status = sModel.checkStatus();
      status.addListener(() -> {
        try
        {
          onStatus(status.get());
        }
        catch (Exception e)
        {
          Logger.w(TAG, "Gemini Nano status unavailable", e);
        }
      }, sExecutor);
    }
    catch (Throwable e)
    {
      // No AICore, unsupported device, or an unlocked bootloader.
      Logger.w(TAG, "Gemini Nano not supported", e);
      sModel = null;
    }
  }

  private static void onStatus(int status)
  {
    switch (status)
    {
    case FeatureStatus.AVAILABLE -> nativeSetAvailable(true);
    case FeatureStatus.DOWNLOADABLE -> sModel.download(new DownloadCallback() {
      @Override
      public void onDownloadStarted(long bytesToDownload) { Logger.i(TAG, "Downloading Gemini Nano"); }
      @Override
      public void onDownloadProgress(long totalBytesDownloaded) {}
      @Override
      public void onDownloadCompleted() { nativeSetAvailable(true); }
      @Override
      public void onDownloadFailed(@NonNull GenAiException e) { Logger.w(TAG, "Gemini Nano download failed", e); }
    });
    default -> Logger.i(TAG, "Gemini Nano status " + status);
    }
  }

  // Called from native code with a line to reword. Answers through nativeOnRewritten.
  @Keep
  @SuppressWarnings("unused")
  private static void rewrite(long id, @NonNull String instructions, @NonNull String prompt)
  {
    GenerativeModelFutures model = sModel;
    if (model == null)
      return;
    try
    {
      GenerateContentRequest.Builder builder =
          new GenerateContentRequest.Builder(new TextPart(instructions + "\n\n" + prompt));
      builder.setTemperature(0.9f);
      builder.setTopK(40);
      builder.setCandidateCount(1);
      ListenableFuture<GenerateContentResponse> future = model.generateContent(builder.build());
      future.addListener(() -> {
        try
        {
          GenerateContentResponse response = future.get();
          if (!response.getCandidates().isEmpty())
            nativeOnRewritten(id, response.getCandidates().get(0).getText());
        }
        catch (Exception e)
        {
          Logger.w(TAG, "Gemini Nano rewrite failed", e);
        }
      }, sExecutor);
    }
    catch (Throwable e)
    {
      Logger.w(TAG, "Gemini Nano rewrite failed", e);
    }
  }

  private static native void nativeSetAvailable(boolean available);
  private static native void nativeOnRewritten(long id, @NonNull String text);
}
