// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 James Turner

#pragma once

#include <functional>
#include <string>
#include <vector>

#include <memory>

#include <set>
#include <simgear/misc/ResourceManager.hxx>
#include <simgear/misc/sg_path.hxx>
#include <simgear/nasal/nasal.h>

#include <simgear/props/props.hxx>

class FGNasalSys;

/**
 * @brief A Nasal module registered under /nasal in the property tree.
 *
 * The node /nasal/<name> describes the module: whether it is enabled, and any
 * files or script it is extended with (by an aircraft or add-on, for example).
 * Files which come from FG_ROOT/Nasal are known to this class only, and are
 * not written into the property tree. A <file> entry in defaults.xml which
 * names a file or directory of FG_ROOT/Nasal selects the files of the module,
 * otherwise those found in the directory of the same name are used. Files
 * added by aircraft are loaded after those, in the module's place in the load
 * order, which is the order of the nodes under /nasal.
 *
 * This class also watches the 'enabled' property, to load and unload the module.
 *
 * Every Nasal namespace has a NasalModule, including those created at runtime
 * by dialogs, models and so on: these have no property node or files. All
 * running of code in a module, and deletion of it, is done here.
 */
class NasalModule : public SGPropertyChangeListener
{
public:
    NasalModule(FGNasalSys* sys, const std::string& name, SGPropertyNode* node = nullptr);

    /**
     * Create the module for a node under /nasal. The name is that of the node
     * (without a 'builtin-' prefix), unless it has a <module> child. <file> children are resolved against the files in
     * 'available' (the files found in the Nasal directories): each entry is
     * a path relative to the Nasal directory, or a directory name ending in
     * '/' for all the files in it.
     */
    static std::unique_ptr<NasalModule> fromProperties(FGNasalSys* sys, SGPropertyNode* node,
                                                       const std::vector<simgear::ResourceManager::FoundFile>& available);
    ~NasalModule() override;

    // the Nasal module name, which can differ from the node's name if the node
    // has a <module> child
    const std::string& name() const { return _name; }
    // null for modules created at runtime, which are not registered under /nasal
    SGPropertyNode* node() const { return _node; }

    // true if defaults.xml names files of FG_ROOT/Nasal in <file> entries, so
    // the module does not use the files found in its directory
    bool hasListedFiles() const { return !_listed.empty(); }
    bool isEnabled() const;
    bool isLoaded() const;

    // files from FG_ROOT/Nasal: those named by <file> entries in defaults.xml,
    // or else all those found in the directory of the same name
    const std::vector<SGPath>& files() const { return _files; }
    void addFile(const SGPath& path);

    // files listed in the property tree, i.e. coming from aircraft or add-ons
    std::vector<std::string> extraFiles() const;

    // hooks for the C++ parts of hybrid modules (gui, canvas): 'load' runs before
    // any files are loaded, 'unload' after the module is deleted
    void setNativeHooks(std::function<void()> load, std::function<void()> unload = {});

    // 'postLoad' runs once all the module's files (including extras) are loaded,
    // for native code which needs to look up things defined by the Nasal code
    void setPostLoadHook(std::function<void()> postLoad);

    // Parse and run code in the module's namespace, creating it if needed. The
    // namespace is shared by all the code run in the module, which can extend a
    // module that is natively implemented. The optional cmdarg is available to
    // the code via cmdarg().
    bool run(const char* fileName, const char* src, int len,
             const SGPropertyNode* cmdarg = nullptr,
             int argc = 0, naRef* args = nullptr,
             int sourceLineOffset = 1);
    bool runFile(const SGPath& path);

    // true if there are files or a script to run again
    bool canReload() const;

    // The native hook, then the files from FG_ROOT/Nasal, then the files and
    // script in the property tree, then the post-load hook
    bool load();

    void unload();

    // Record that the Nasal module was deleted, other than by unload(), e.g.
    // FGNasalSys::deleteModule. Modules can watch 'loaded' to trigger unload behaviours.
    void markUnloaded();

    // Create the 'enabled' property if the module has none, e.g. it is not
    // listed in defaults.xml. Returns true if it was created.
    bool setEnabledByDefault(bool enabled);

    // start watching the 'enabled' property, to load and unload on demand
    void watchEnabled();

    // true if all the files of the module have valid syntax
    bool checkFiles() const;

private:
    void valueChanged(SGPropertyNode* node) override;

    bool loadFile(const std::string& file);
    void setLoaded(bool b);

    FGNasalSys* _sys;
    std::string _name;
    SGPropertyNode_ptr _node;
    std::vector<SGPath> _files;
    // <file> entries resolved into _files
    std::set<std::string> _listed;
    std::function<void()> _loadHook, _unloadHook, _postLoadHook;
    bool _hookRun = false;
    bool _loaded = false;
    bool _loadOk = true;
    bool _listening = false;
};
