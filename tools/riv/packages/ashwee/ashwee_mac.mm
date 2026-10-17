// macOS implementation of $(<ashwee>), with AppKit (Cocoa). Compiled once as
// Objective-C++ (ARC) and linked into programs that import ashwee.
// The runtime first: Objective-C defines a macro Nil, which is a type in Dessa
#include "ashwee.hpp"

#include <deque>
#include <vector>

#import <Cocoa/Cocoa.h>
#undef Nil

namespace dessa::ashwee {
void ashweePush(const char* kind, Int itemId);
}

// Top-left coordinates, like on Windows and Linux
@interface DessaFlippedView : NSView
@end
@implementation DessaFlippedView
- (BOOL)isFlipped { return YES; }
@end

// Receives the clicks, typing, selections and close requests of one window or control
@interface DessaTarget : NSObject <NSWindowDelegate, NSTextFieldDelegate, NSTextViewDelegate, NSTableViewDataSource, NSTableViewDelegate>
@property(nonatomic) long long itemId;
@property(nonatomic, strong) NSMutableArray<NSString*>* rows;
@end

@implementation DessaTarget
- (void)clicked:(id)sender { dessa::ashwee::ashweePush("click", self.itemId); }
- (void)controlTextDidChange:(NSNotification*)note { dessa::ashwee::ashweePush("change", self.itemId); }
- (void)textDidChange:(NSNotification*)note { dessa::ashwee::ashweePush("change", self.itemId); }
- (void)tableViewSelectionDidChange:(NSNotification*)note { dessa::ashwee::ashweePush("select", self.itemId); }
- (BOOL)windowShouldClose:(NSWindow*)sender {
    dessa::ashwee::ashweePush("close", self.itemId);
    return NO;   // ashwee.dm decides (it calls _close)
}
- (NSInteger)numberOfRowsInTableView:(NSTableView*)table { return (NSInteger)self.rows.count; }
- (id)tableView:(NSTableView*)table objectValueForTableColumn:(NSTableColumn*)column row:(NSInteger)row {
    return self.rows[(NSUInteger)row];
}
@end

namespace dessa::ashwee {

namespace {

struct Item {
    __strong id view = nil;          // NSWindow, NSTextField, NSButton, NSTextView or NSTableView
    __strong id outer = nil;         // what sits in the window (a scroll view for textarea / list)
    __strong DessaTarget* target = nil;
    Int window = -1;
    Str kind;
    bool open = true;
};

std::vector<Item> items;
std::deque<List<Str>> events;
bool started = false;

NSString* ns(const Str& text) { return [NSString stringWithUTF8String:text.c_str()] ?: @""; }
Str str(NSString* text) { return text ? Str([text UTF8String]) : Str(); }

Item& item(Int itemId, const char* what) {
    if (itemId < 0 || itemId >= static_cast<Int>(items.size()) || !items[static_cast<size_t>(itemId)].open) {
        fail(std::string("ashwee: ") + what + ": no such window or control (" + std::to_string(itemId) + ")");
    }
    return items[static_cast<size_t>(itemId)];
}

void start() {
    if (started) return;
    [NSApplication sharedApplication];
    [NSApp setActivationPolicy:NSApplicationActivationPolicyRegular];
    [NSApp finishLaunching];
    started = true;
}

DessaTarget* targetFor(Int itemId) {
    DessaTarget* target = [[DessaTarget alloc] init];
    target.itemId = itemId;
    return target;
}

} // namespace

void ashweePush(const char* kind, Int itemId) {
    if (itemId >= 0 && itemId < static_cast<Int>(items.size()) && items[static_cast<size_t>(itemId)].open) {
        events.push_back({kind, std::to_string(itemId)});
    }
}

Int _window(const Str& title, Int width, Int height) {
    @autoreleasepool {
        start();
        NSRect frame = NSMakeRect(200, 200, (CGFloat)width, (CGFloat)height);
        NSWindow* win = [[NSWindow alloc] initWithContentRect:frame
                                                    styleMask:(NSWindowStyleMaskTitled | NSWindowStyleMaskClosable | NSWindowStyleMaskMiniaturizable)
                                                      backing:NSBackingStoreBuffered
                                                        defer:NO];
        win.releasedWhenClosed = NO;
        win.title = ns(title);
        win.contentView = [[DessaFlippedView alloc] initWithFrame:frame];
        Int newId = static_cast<Int>(items.size());   // (not "id": that's the Objective-C type)
        DessaTarget* target = targetFor(newId);
        win.delegate = target;
        Item it;
        it.view = win;
        it.outer = win.contentView;
        it.target = target;
        it.window = newId;
        it.kind = "window";
        items.push_back(it);
        return newId;
    }
}

Int _control(Int window, const Str& kind, const Str& text, Int x, Int y, Int width, Int height) {
    @autoreleasepool {
        NSView* content = item(window, "add a control").outer;
        Int newId = static_cast<Int>(items.size());   // (not "id": that's the Objective-C type)
        DessaTarget* target = targetFor(newId);
        NSRect frame = NSMakeRect((CGFloat)x, (CGFloat)y, (CGFloat)width, (CGFloat)height);
        id view = nil;
        NSView* outer = nil;
        if (kind == "label") {
            NSTextField* label = [NSTextField labelWithString:ns(text)];
            view = label;
        } else if (kind == "button") {
            NSButton* button = [NSButton buttonWithTitle:ns(text) target:target action:@selector(clicked:)];
            button.bezelStyle = NSBezelStyleRounded;
            view = button;
        } else if (kind == "checkbox") {
            view = [NSButton checkboxWithTitle:ns(text) target:target action:@selector(clicked:)];
        } else if (kind == "input" || kind == "password") {
            NSTextField* field = kind == "password" ? [[NSSecureTextField alloc] initWithFrame:frame]
                                                     : [NSTextField textFieldWithString:ns(text)];
            if (kind == "password") field.stringValue = ns(text);
            field.delegate = target;
            view = field;
        } else if (kind == "textarea") {
            NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:frame];
            scroll.hasVerticalScroller = YES;
            scroll.borderType = NSBezelBorder;
            NSTextView* textView = [[NSTextView alloc] initWithFrame:NSMakeRect(0, 0, frame.size.width, frame.size.height)];
            textView.string = ns(text);
            textView.delegate = target;
            textView.autoresizingMask = NSViewWidthSizable;
            scroll.documentView = textView;
            view = textView;
            outer = scroll;
        } else if (kind == "list") {
            NSScrollView* scroll = [[NSScrollView alloc] initWithFrame:frame];
            scroll.hasVerticalScroller = YES;
            scroll.borderType = NSBezelBorder;
            NSTableView* table = [[NSTableView alloc] initWithFrame:NSMakeRect(0, 0, frame.size.width, frame.size.height)];
            NSTableColumn* column = [[NSTableColumn alloc] initWithIdentifier:@"items"];
            column.width = frame.size.width;
            [table addTableColumn:column];
            table.headerView = nil;
            target.rows = [NSMutableArray array];
            table.dataSource = target;
            table.delegate = target;
            scroll.documentView = table;
            view = table;
            outer = scroll;
        } else {
            fail("ashwee: unknown control kind '" + kind + "'");
        }
        if (!outer) outer = view;
        outer.frame = frame;
        [content addSubview:outer];
        Item it;
        it.view = view;
        it.outer = outer;
        it.target = target;
        it.window = window;
        it.kind = kind;
        items.push_back(it);
        return newId;
    }
}

void _show(Int window) {
    NSWindow* win = item(window, "show").view;
    [win makeKeyAndOrderFront:nil];
    [NSApp activateIgnoringOtherApps:YES];
}

void _close(Int window) {
    Item& w = item(window, "close");
    NSWindow* win = w.view;
    win.delegate = nil;
    [win close];
    for (auto& i : items) {
        if (i.window == window) i.open = false;
    }
}

void _setText(Int itemId, const Str& text) {
    Item& i = item(itemId, "setText");
    if (i.kind == "window") ((NSWindow*)i.view).title = ns(text);
    else if (i.kind == "button" || i.kind == "checkbox") ((NSButton*)i.view).title = ns(text);
    else if (i.kind == "textarea") ((NSTextView*)i.view).string = ns(text);
    else if (i.kind != "list") ((NSTextField*)i.view).stringValue = ns(text);
}

Str _getText(Int itemId) {
    Item& i = item(itemId, "text");
    if (i.kind == "window") return str(((NSWindow*)i.view).title);
    if (i.kind == "button" || i.kind == "checkbox") return str(((NSButton*)i.view).title);
    if (i.kind == "textarea") return str(((NSTextView*)i.view).string);
    if (i.kind == "list") return "";
    return str(((NSTextField*)i.view).stringValue);
}

void _setEnabled(Int itemId, Bul enabled) {
    Item& i = item(itemId, "setEnabled");
    if (i.kind == "textarea") ((NSTextView*)i.view).editable = enabled;
    else if (i.kind == "list") ((NSTableView*)i.view).enabled = enabled;
    else if (i.kind != "window") ((NSControl*)i.view).enabled = enabled;
}

void _setChecked(Int itemId, Bul checked) {
    ((NSButton*)item(itemId, "setChecked").view).state = checked ? NSControlStateValueOn : NSControlStateValueOff;
}

Bul _isChecked(Int itemId) {
    return ((NSButton*)item(itemId, "checked").view).state == NSControlStateValueOn;
}

void _listAdd(Int itemId, const Str& entry) {
    Item& i = item(itemId, "add");
    [i.target.rows addObject:ns(entry)];
    [(NSTableView*)i.view reloadData];
}

void _listClear(Int itemId) {
    Item& i = item(itemId, "clear");
    [i.target.rows removeAllObjects];
    [(NSTableView*)i.view reloadData];
}

Int _listSelected(Int itemId) {
    NSInteger row = ((NSTableView*)item(itemId, "selected").view).selectedRow;
    return row < 0 ? -1 : (Int)row;
}

void _message(const Str& title, const Str& text) {
    @autoreleasepool {
        start();
        NSAlert* alert = [[NSAlert alloc] init];
        alert.messageText = ns(title);
        alert.informativeText = ns(text);
        [alert addButtonWithTitle:@"OK"];
        [alert runModal];
    }
}

List<Str> _nextEvent() {
    while (events.empty()) {
        bool anyOpen = false;
        for (const auto& i : items) {
            if (i.open && i.kind == "window") anyOpen = true;
        }
        if (!anyOpen) return {"idle", ""};
        @autoreleasepool {
            NSEvent* event = [NSApp nextEventMatchingMask:NSEventMaskAny
                                                untilDate:[NSDate distantFuture]
                                                   inMode:NSDefaultRunLoopMode
                                                  dequeue:YES];
            if (event) [NSApp sendEvent:event];
            [NSApp updateWindows];
        }
    }
    List<Str> event = events.front();
    events.pop_front();
    return event;
}

} // namespace dessa::ashwee
