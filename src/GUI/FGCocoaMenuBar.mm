// SPDX-FileCopyrightText: (C) 2011 James Turner <james@flightgear.org>
// SPDX-License-Identifier: GPL-2.0-or-later

#include "FGCocoaMenuBar.hxx"

#include <AppKit/NSMenu.h>
#include <AppKit/NSApplication.h>

#include <simgear/props/props.hxx>
#include <simgear/props/props_io.hxx>
#include <simgear/debug/logstream.hxx>
#include <simgear/structure/SGBinding.hxx>
#include <simgear/misc/strutils.hxx>
#include <simgear/sg_inlines.h>

#include <Main/fg_props.hxx>
#include <GUI/CocoaHelpers_private.h>
#include <Main/sentryIntegration.hxx>

#include <iostream>

using std::string;
using std::map;
using std::cout;
using namespace simgear;

typedef std::map<NSMenuItem*, SGBindingList> MenuItemBindings;

@class CocoaMenuDelegate;

namespace {

    class CocoaItemListener : public SGPropertyChangeListener
    {
    public:
        CocoaItemListener(SGPropertyNode_ptr prop, NSMenuItem* i) :
            enableProp(prop->getNode("enabled")),
            checkedProp(prop->getNode("checked")),
            item(i)
        {
            if (enableProp) {
                enableProp->addChangeListener(this);
                BOOL b = enableProp->getBoolValue();
                [item setEnabled:b];
            }

            if (checkedProp) {
                checkedProp->addChangeListener(this);
                auto b = checkedProp->getBoolValue();
                item.state = b ? NSControlStateValueOn : NSControlStateValueOff;
            }
        }

        ~CocoaItemListener()
        {
            if (enableProp) {
                enableProp->removeChangeListener(this);
            }
            if (checkedProp) {
                checkedProp->removeChangeListener(this);
            }
        }

        void valueChanged(SGPropertyNode *node) override {
          CocoaAutoreleasePool pool;
          if (node == enableProp) {
            BOOL b = node->getBoolValue();
            [item setEnabled:b];
          }

          if (node == checkedProp) {
            auto b = node->getBoolValue();
            item.state = b ? NSControlStateValueOn : NSControlStateValueOff;
          }
        }

    private:
        SGPropertyNode_ptr enableProp;
        SGPropertyNode_ptr checkedProp;
        NSMenuItem* item;
    };
} // of anonymous namespace

class FGCocoaMenuBar::CocoaMenuBarPrivate
{
public:
  CocoaMenuBarPrivate();
  ~CocoaMenuBarPrivate();

  void menuFromProps(NSMenu* menu, SGPropertyNode* menuNode);

  void fireBindingsForItem(NSMenuItem* item);

public:
  CocoaMenuDelegate* delegate;

  MenuItemBindings itemBindings;
  std::vector<CocoaItemListener *> listeners;
};

@interface CocoaMenuDelegate : NSObject <NSMenuDelegate> {
@private
  FGCocoaMenuBar::CocoaMenuBarPrivate* peer;
}

@property (nonatomic, assign) FGCocoaMenuBar::CocoaMenuBarPrivate* peer;
@end

@implementation CocoaMenuDelegate

@synthesize peer;

- (void) itemAction:(id) sender
{
  peer->fireBindingsForItem((NSMenuItem*) sender);
}

@end

static void setFunctionKeyShortcut(const std::string& shortcut, NSMenuItem* item)
{
    unichar shortcutChar = NSF1FunctionKey;
    if (shortcut == "F1") {
        shortcutChar = NSF1FunctionKey;
    } else if (shortcut == "F2") {
        shortcutChar = NSF2FunctionKey;
    } else if (shortcut == "F3") {
        shortcutChar = NSF3FunctionKey;
    } else if (shortcut == "F10") {
        shortcutChar = NSF10FunctionKey;
    } else if (shortcut == "F11") {
        shortcutChar = NSF11FunctionKey;
    } else if (shortcut == "F12") {
        shortcutChar = NSF12FunctionKey;
    } else {
        SG_LOG(SG_GENERAL, SG_WARN, "CocoaMenu:setFunctionKeyShortcut: unsupported:" << shortcut);
    }

  unichar ch[1];
  ch[0] = shortcutChar;
  [item setKeyEquivalentModifierMask:NSEventModifierFlagFunction];
  [item setKeyEquivalent:[NSString stringWithCharacters:ch length:1]];
}



static void setItemShortcutFromString(NSMenuItem* item, const string& s)
{
    std::string shortcut;

    bool hasCtrl = strutils::starts_with(s, "Ctrl-");
    bool hasShift = strutils::starts_with(s, "Shift-");
    bool hasAlt = strutils::starts_with(s, "Alt-");

    int offset = 0; // character offset from start of string
    if (hasShift)
      offset += 6;
    if (hasCtrl)
      offset += 5;
    if (hasAlt)
      offset += 4;

    shortcut = s.substr(offset);
    if (shortcut == "Esc") {
      shortcut = "\e";
    }

    if ((shortcut.length() >= 2) && (shortcut[0] == 'F') && isdigit(shortcut[1])) {
        setFunctionKeyShortcut(shortcut, item);
        return;
    }

  shortcut = simgear::strutils::lowercase(shortcut);
  [item setKeyEquivalent:[NSString stringWithCString:shortcut.c_str() encoding:NSUTF8StringEncoding]];
  NSUInteger modifiers = 0;
  if (hasCtrl) modifiers |= NSEventModifierFlagControl;
  if (hasShift) modifiers |= NSEventModifierFlagShift;
  if (hasAlt) modifiers |= NSEventModifierFlagOption;

  [item setKeyEquivalentModifierMask:modifiers];
}

static bool doesBindingShowDialog(const SGBindingList &bindings) {
  auto it = std::find_if(bindings.begin(), bindings.end(),
                         [](SGAbstractBinding_ptr binding) {
                           auto nab = dynamic_cast<SGBinding *>(binding.get());
                           if (!nab) {
                             return false;
                           }

                           return nab->getCommandName() == "dialog-show";
                         });

  return it != bindings.end();
}

FGCocoaMenuBar::CocoaMenuBarPrivate::CocoaMenuBarPrivate()
{
  delegate = [[CocoaMenuDelegate alloc] init];
  delegate.peer = this;
}

FGCocoaMenuBar::CocoaMenuBarPrivate::~CocoaMenuBarPrivate()
{
  CocoaAutoreleasePool pool;
  [delegate release];
}

static bool labelIsSeparator(NSString* s)
{
  return [s hasPrefix:@"---"];
}

void FGCocoaMenuBar::CocoaMenuBarPrivate::menuFromProps(NSMenu* menu, SGPropertyNode* menuNode)
{
  int index = 0;
  for (SGPropertyNode_ptr n : menuNode->getChildren("item")) {
    if (!n->hasValue("enabled")) {
      n->setBoolValue("enabled", true);
    }

    SGBindingList bl = readBindingList(n->getChildren("binding"), globals->get_props());
    bool showsDialog = n->getBoolValue("shows-dialog") || doesBindingShowDialog(bl);

    string l = strutils::simplify(getLocalizedLabel(n));
    if (showsDialog && !strutils::ends_with(l, "...")) {
      l += reinterpret_cast<const char *>(u8"…");
    }

    NSString* label = stdStringToCocoa(l);
    string shortcut = n->getStringValue("key");

    const bool isSeparator = labelIsSeparator(label);
    NSMenuItem *item = nil;
    if (index < [menu numberOfItems]) {
      item = [menu itemAtIndex:index];
      if ([item isSeparatorItem] != isSeparator) {
        // structure differs, replace this one item
        [menu removeItemAtIndex:index];
        item = nil;
      }
    }

    if (isSeparator) {
      if (!item) {
        item = [NSMenuItem separatorItem];
        [menu insertItem:item atIndex:index];
      }
    } else {
      if (!item) {
        item = [menu insertItemWithTitle:label
                                  action:nil
                           keyEquivalent:@""
                                 atIndex:index];
      } else {
        [item setTitle:label];
      }

      // (re)apply target, since the delegate may have changed since the
      // item was created
      [item setKeyEquivalent:@""];
      [item setKeyEquivalentModifierMask:0];
      if (!shortcut.empty()) {
        setItemShortcutFromString(item, shortcut);
      }
      [item setTarget:delegate];
      [item setAction:@selector(itemAction:)];

      listeners.push_back(new CocoaItemListener(n, item));
    }

    auto subMenuNode = n->getChild("menu");
    if (subMenuNode) {
      NSMenu *subMenu = [item submenu];
      if (!subMenu) {
        subMenu = [[[NSMenu alloc] init] autorelease];
        [item setSubmenu:subMenu];
      }
      menuFromProps(subMenu, subMenuNode);
    }

    itemBindings[item] = bl;
    ++index;
  } // of item iteration

  // remove any surplus items from a previous build
  while ([menu numberOfItems] > index) {
    [menu removeItemAtIndex:index];
  }
}

void FGCocoaMenuBar::CocoaMenuBarPrivate::fireBindingsForItem(NSMenuItem *item)
{
  MenuItemBindings::iterator it = itemBindings.find(item);
  if (it == itemBindings.end()) {
    return;
  }

    NSString* label = [item title];
    const auto s = stdStringFromCocoa(label);
    flightgear::addSentryBreadcrumb("fire menu item:" + s, "info");
  fireBindingList(it->second);
}

FGCocoaMenuBar::FGCocoaMenuBar() :
  p(new CocoaMenuBarPrivate)
{

}

FGCocoaMenuBar::~FGCocoaMenuBar()
{
    CocoaAutoreleasePool ap;
    NSMenu* mainBar = [[NSApplication sharedApplication] mainMenu];

    int num = [mainBar numberOfItems];
    for (int index=1; index < num; ++index) {
        NSMenuItem* topLevelItem = [mainBar itemAtIndex:index];
        [topLevelItem.submenu removeAllItems];
    }

    for (auto l : p->listeners) {
      delete l;
    }
}

void FGCocoaMenuBar::init()
{
  CocoaAutoreleasePool pool;

  NSMenu* mainBar = [[NSApplication sharedApplication] mainMenu];
  SGPropertyNode_ptr props = fgGetNode("/sim/menubar/default",true);

  int index = 0;
  NSMenuItem* previousMenu = [mainBar itemAtIndex:0];
  if (![[previousMenu title] isEqualToString:@"FlightGear"]) {
    [previousMenu setTitle:@"FlightGear"];
  }

  // listeners and bindings reference items we are about to (re)synchronise
  for (auto l : p->listeners) {
    delete l;
  }
  p->listeners.clear();
  p->itemBindings.clear();

  // clear other menus in the menu-bar, eg from the launcher. Menus we
  // created previously (tagged >= 400) are kept and reused, since replacing
  // top-level items confuses AppKit's menu tracking.
  for (NSInteger i = [mainBar numberOfItems] - 1; i >= 1; --i) {
    if ([[mainBar itemAtIndex:i] tag] < 400) {
      [mainBar removeItemAtIndex:i];
    }
  }

  for (auto n : props->getChildren("menu")) {
    NSString* label = stdStringToCocoa(getLocalizedLabel(n));
    NSMenuItem* item = [mainBar itemWithTitle:label];
    if (item && [item tag] < 400) {
      item = nil;
    }
    NSMenu* menu;

    if (!item) {
      NSInteger insertIndex = [mainBar indexOfItem:previousMenu] + 1;
      item = [mainBar insertItemWithTitle:label action:nil keyEquivalent:@"" atIndex:insertIndex];
      item.tag = index + 400;

      menu = [[NSMenu alloc] init];
      menu.title = label;
      [menu setAutoenablesItems:NO];
      [mainBar setSubmenu:menu forItem:item];
      [menu autorelease];
    } else {
      menu = item.submenu;
      item.tag = index + 400;
    }

  // synchronise menu with properties
    p->menuFromProps(menu, n);
    ++index;
    previousMenu = item;

  // track menu enable/disable state
    if (!n->hasValue("enabled")) {
      n->setBoolValue("enabled", true);
    }

    auto l = new CocoaItemListener( n, item);
    p->listeners.push_back(l);
  }
}

bool FGCocoaMenuBar::isVisible() const
{
  return true;
}

void FGCocoaMenuBar::show()
{
  // no-op
}

void FGCocoaMenuBar::hide()
{
  // no-op
}

void FGCocoaMenuBar::setHideIfOverlapsWindow(bool hide)
{
    SG_UNUSED(hide);
    // no-op
}

bool FGCocoaMenuBar::getHideIfOverlapsWindow() const { return false; }
