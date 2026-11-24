#import <Cocoa/Cocoa.h>
#import <Metal/Metal.h>
#import <MetalKit/MetalKit.h>

#include "../Core/CommanderEngine.hpp"
#include "../Core/Version.hpp"
#include "../UI/AntigravityTheme.hpp"
#include "../UI/CommanderUI.hpp"
#include "../Vendor/imgui/backends/imgui_impl_metal.h"
#include "../Vendor/imgui/backends/imgui_impl_osx.h"
#include "../Vendor/imgui/imgui.h"

#include <memory>

@interface MCommanderViewController : NSViewController <MTKViewDelegate>
@property(nonatomic, readonly) MTKView *mtkView;
@property(nonatomic, strong) id<MTLDevice> device;
@property(nonatomic, strong) id<MTLCommandQueue> commandQueue;
@end

@implementation MCommanderViewController {
  std::unique_ptr<mc::CommanderEngine> _engine;
  std::unique_ptr<mc::ui::CommanderUI> _ui;
}

- (MTKView *)mtkView {
  return (MTKView *)self.view;
}

- (void)loadView {
  self.device = MTLCreateSystemDefaultDevice();
  MTKView *view = [[MTKView alloc] initWithFrame:NSMakeRect(0, 0, 1340, 840)
                                          device:self.device];
  view.colorPixelFormat = MTLPixelFormatBGRA8Unorm;
  view.clearColor = MTLClearColorMake(0.153, 0.157, 0.133, 1.0);
  view.preferredFramesPerSecond = 60;
  view.delegate = self;
  self.view = view;
}

- (void)viewDidLoad {
  [super viewDidLoad];
  self.commandQueue = [self.device newCommandQueue];

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGuiIO &io = ImGui::GetIO();
  io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
  io.IniFilename = nullptr; // Do not clutter disk with imgui.ini

  mc::ui::ThemeFonts fonts = mc::ui::applyAntigravityTheme();

  ImGui_ImplMetal_Init(self.device);
  ImGui_ImplOSX_Init(self.view);

  _engine = std::make_unique<mc::CommanderEngine>();
  _ui = std::make_unique<mc::ui::CommanderUI>(*_engine, fonts);
}

- (void)dealloc {
  _ui.reset();
  _engine.reset();
  ImGui_ImplMetal_Shutdown();
  ImGui_ImplOSX_Shutdown();
  ImGui::DestroyContext();
}

- (void)mtkView:(MTKView *)view drawableSizeWillChange:(CGSize)size {
  (void)view;
  (void)size;
}

- (void)drawInMTKView:(MTKView *)view {
  ImGuiIO &io = ImGui::GetIO();
  io.DisplaySize.x = view.bounds.size.width;
  io.DisplaySize.y = view.bounds.size.height;

  CGFloat framebufferScale = view.window.screen.backingScaleFactor
                                 ?: NSScreen.mainScreen.backingScaleFactor;
  io.DisplayFramebufferScale =
      ImVec2((float)framebufferScale, (float)framebufferScale);

  id<MTLCommandBuffer> commandBuffer = [self.commandQueue commandBuffer];
  MTLRenderPassDescriptor *renderPassDescriptor =
      view.currentRenderPassDescriptor;
  if (!renderPassDescriptor) {
    [commandBuffer commit];
    return;
  }

  ImGui_ImplMetal_NewFrame(renderPassDescriptor);
  ImGui_ImplOSX_NewFrame(view);
  ImGui::NewFrame();

  if (_ui) {
    _ui->render();
  }

  ImGui::Render();
  ImDrawData *drawData = ImGui::GetDrawData();

  id<MTLRenderCommandEncoder> renderEncoder =
      [commandBuffer renderCommandEncoderWithDescriptor:renderPassDescriptor];
  [renderEncoder pushDebugGroup:@"MCommander ImGui"];
  ImGui_ImplMetal_RenderDrawData(drawData, commandBuffer, renderEncoder);
  [renderEncoder popDebugGroup];
  [renderEncoder endEncoding];

  [commandBuffer presentDrawable:view.currentDrawable];
  [commandBuffer commit];
}

@end

@interface MCommanderAppDelegate : NSObject <NSApplicationDelegate>
@property(nonatomic, strong) NSWindow *window;
@end

@implementation MCommanderAppDelegate

- (void)setupMainMenu {
  NSMenu *mainMenu = [[NSMenu alloc] init];
  NSMenuItem *appMenuItem = [[NSMenuItem alloc] init];
  [mainMenu addItem:appMenuItem];

  NSMenu *appMenu = [[NSMenu alloc] init];
  [appMenu addItemWithTitle:@"O programie mcx"
                     action:@selector(orderFrontStandardAboutPanel:)
              keyEquivalent:@""];
  [appMenu addItem:[NSMenuItem separatorItem]];
  [appMenu addItemWithTitle:@"Zakończ mcx"
                     action:@selector(terminate:)
              keyEquivalent:@"q"];
  [appMenuItem setSubmenu:appMenu];

  NSMenuItem *editMenuItem = [[NSMenuItem alloc] init];
  [mainMenu addItem:editMenuItem];
  NSMenu *editMenu = [[NSMenu alloc] initWithTitle:@"Edycja"];
  [editMenu addItemWithTitle:@"Kopiuj"
                      action:@selector(copy:)
               keyEquivalent:@"c"];
  [editMenu addItemWithTitle:@"Wklej"
                      action:@selector(paste:)
               keyEquivalent:@"v"];
  [editMenu addItemWithTitle:@"Wytnij"
                      action:@selector(cut:)
               keyEquivalent:@"x"];
  [editMenu addItemWithTitle:@"Zaznacz wszystko"
                      action:@selector(selectAll:)
               keyEquivalent:@"a"];
  [editMenuItem setSubmenu:editMenu];

  [NSApp setMainMenu:mainMenu];
}

- (void)applicationDidFinishLaunching:(NSNotification *)notification {
  (void)notification;
  [self setupMainMenu];

  NSWindowStyleMask style =
      NSWindowStyleMaskTitled | NSWindowStyleMaskClosable |
      NSWindowStyleMaskMiniaturizable | NSWindowStyleMaskResizable;

    self.window = [[NSWindow alloc] initWithContentRect:NSMakeRect(0, 0, 1340, 840)
                                              styleMask:style
                                                backing:NSBackingStoreBuffered
                                                  defer:NO];
    self.window.title = @"mcx " MCX_VERSION;
    self.window.minSize = NSMakeSize(960, 580);
    self.window.appearance = [NSAppearance appearanceNamed:NSAppearanceNameDarkAqua];
    self.window.backgroundColor = [NSColor colorWithRed:0.153 green:0.157 blue:0.133 alpha:1.0];
    self.window.titlebarAppearsTransparent = YES;

  MCommanderViewController *vc = [[MCommanderViewController alloc] init];
  self.window.contentViewController = vc;
  [self.window center];
  [self.window makeKeyAndOrderFront:nil];
  [NSApp activateIgnoringOtherApps:YES];
}

- (BOOL)applicationShouldTerminateAfterLastWindowClosed:
    (NSApplication *)sender {
  (void)sender;
  return YES;
}

@end

int main(int argc, const char *argv[]) {
  (void)argc;
  (void)argv;
  @autoreleasepool {
    NSApplication *app = [NSApplication sharedApplication];
    [app setActivationPolicy:NSApplicationActivationPolicyRegular];
    MCommanderAppDelegate *delegate = [[MCommanderAppDelegate alloc] init];
    [app setDelegate:delegate];
    [app run];
  }
  return 0;
}
