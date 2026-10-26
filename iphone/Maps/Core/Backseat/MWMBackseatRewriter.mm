#import "MWMBackseatRewriter.h"

#include <CoreApi/Framework.h>

#include "backseat/rewriter.hpp"

#include <atomic>
#include <memory>

namespace
{
class BlockRewriter : public backseat::LineRewriter
{
public:
  explicit BlockRewriter(MWMBackseatRewriteHandler handler) : m_handler([handler copy]) {}

  bool Available() const override { return m_available; }

  void Rewrite(backseat::RewriteRequest const & request, Done done) override
  {
    NSString * instructions = @(backseat::RewriteInstructions().c_str());
    NSString * prompt = @(backseat::RewritePrompt(request).c_str());
    auto shared = std::make_shared<Done>(std::move(done));
    MWMBackseatRewriteHandler handler = m_handler;
    dispatch_async(dispatch_get_global_queue(QOS_CLASS_USER_INITIATED, 0), ^{
      handler(instructions, prompt, ^(NSString * text) { (*shared)(text.UTF8String ?: ""); });
    });
  }

  void SetAvailable(bool available) { m_available = available; }

private:
  MWMBackseatRewriteHandler m_handler;
  std::atomic<bool> m_available{false};
};

std::shared_ptr<BlockRewriter> & Installed()
{
  static std::shared_ptr<BlockRewriter> rewriter;
  return rewriter;
}
}  // namespace

@implementation MWMBackseatRewriter

+ (void)installWithHandler:(MWMBackseatRewriteHandler)handler
{
  auto & rewriter = Installed();
  rewriter = std::make_shared<BlockRewriter>(handler);
  GetFramework().GetRoutingManager().SetBackseatRewriter(rewriter);
}

+ (void)setAvailable:(BOOL)available
{
  if (auto const & rewriter = Installed())
    rewriter->SetAvailable(available);
}

+ (void)driverSaid:(NSString *)text
{
  GetFramework().GetRoutingManager().BackseatDriverSaid(text.UTF8String ?: "");
}

@end
