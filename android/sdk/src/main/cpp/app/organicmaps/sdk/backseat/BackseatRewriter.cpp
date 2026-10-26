// Bridges Backseat Driver's LineRewriter to Gemini Nano (ML Kit Prompt API) in
// app.organicmaps.sdk.backseat.BackseatRewriter.
#include "app/organicmaps/sdk/Framework.hpp"
#include "app/organicmaps/sdk/core/jni_helper.hpp"

#include "map/routing_manager.hpp"

#include "backseat/rewriter.hpp"

#include <atomic>
#include <iomanip>
#include <map>
#include <memory>
#include <mutex>
#include <sstream>

namespace
{
class GeminiNanoRewriter : public backseat::LineRewriter
{
public:
  bool Available() const override { return m_available; }

  void Rewrite(backseat::RewriteRequest const & request, Done done) override
  {
    JNIEnv * env = jni::GetEnv();
    if (env == nullptr)
      return;
    int64_t id;
    {
      std::lock_guard lock(m_mutex);
      id = ++m_nextId;
      m_waiting[id] = std::move(done);
      // Answers that never came back: forget all but the latest few.
      while (m_waiting.size() > 16)
        m_waiting.erase(m_waiting.begin());
    }
    static jclass const clazz = jni::GetGlobalClassRef(env, "app/organicmaps/sdk/backseat/BackseatRewriter");
    static jmethodID const rewrite =
        jni::GetStaticMethodID(env, clazz, "rewrite", "(JLjava/lang/String;Ljava/lang/String;)V");
    jni::TScopedLocalRef instructions(env, jni::ToJavaString(env, backseat::RewriteInstructions()));
    jni::TScopedLocalRef prompt(env, jni::ToJavaString(env, backseat::RewritePrompt(request)));
    env->CallStaticVoidMethod(clazz, rewrite, static_cast<jlong>(id), instructions.get(), prompt.get());
    jni::HandleJavaException(env);
  }

  void SetAvailable(bool available) { m_available = available; }

  void Answer(int64_t id, std::string const & text)
  {
    Done done;
    {
      std::lock_guard lock(m_mutex);
      auto const it = m_waiting.find(id);
      if (it == m_waiting.end())
        return;
      done = std::move(it->second);
      m_waiting.erase(it);
    }
    done(text);
  }

private:
  std::atomic<bool> m_available{false};
  std::mutex m_mutex;
  int64_t m_nextId = 0;
  std::map<int64_t, Done> m_waiting;
};

std::shared_ptr<GeminiNanoRewriter> & Rewriter()
{
  static std::shared_ptr<GeminiNanoRewriter> rewriter;
  return rewriter;
}
}  // namespace

extern "C"
{
JNIEXPORT void JNICALL Java_app_organicmaps_sdk_backseat_BackseatRewriter_nativeSetAvailable(JNIEnv *, jclass,
                                                                                              jboolean available)
{
  auto & rewriter = Rewriter();
  if (!rewriter)
  {
    rewriter = std::make_shared<GeminiNanoRewriter>();
    frm()->GetRoutingManager().SetBackseatRewriter(rewriter);
  }
  rewriter->SetAvailable(available);
}

JNIEXPORT void JNICALL Java_app_organicmaps_sdk_backseat_BackseatRewriter_nativeOnRewritten(JNIEnv * env, jclass,
                                                                                             jlong id, jstring text)
{
  if (auto const & rewriter = Rewriter())
    rewriter->Answer(id, jni::ToNativeString(env, text));
}

JNIEXPORT jstring JNICALL Java_app_organicmaps_sdk_backseat_Backseat_nativeDriverSaid(JNIEnv * env, jclass, jstring text)
{
  // "" when the navigator took it, "route\tlat\tlon\tname" or "search\tquery" for a destination.
  auto const heard = frm()->GetRoutingManager().BackseatDriverSaid(jni::ToNativeString(env, text));
  using Kind = BackseatAdapter::HeardCommand::Kind;
  std::string out;
  if (heard.kind == Kind::RouteTo)
  {
    std::ostringstream s;
    s << std::setprecision(9) << "route\t" << heard.lat << '\t' << heard.lon << '\t' << heard.name;
    out = s.str();
  }
  else if (heard.kind == Kind::Search)
    out = "search\t" + heard.query;
  return jni::ToJavaString(env, out);
}
}  // extern "C"
