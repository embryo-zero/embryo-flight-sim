// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2026 James Turner

#include "config.h"

#include "NasalModule.hxx"

#include <algorithm>

#include <simgear/debug/ErrorReportingCallback.hxx>
#include <simgear/debug/logstream.hxx>
#include <simgear/misc/strutils.hxx>

#include <cstring>

#include <simgear/io/sg_mmap.hxx>

#include "NasalSys.hxx"
#include "NasalSys_private.hxx"
#include <Main/globals.hxx>

namespace {

const std::string BuiltinPrefix = "builtin-";

// Resolve the path stored in a module's <file> node
SGPath resolveModuleFilePath(const std::string& file)
{
    SGPath p(file);
    if (!p.isAbsolute() || !p.exists()) {
        p = globals->resolve_maybe_aircraft_path(file);
    }
    return p;
}

} // namespace

std::unique_ptr<NasalModule> NasalModule::fromProperties(FGNasalSys* sys, SGPropertyNode* node,
                                                         const std::vector<simgear::ResourceManager::FoundFile>& available)
{
    // The entries in defaults.xml for top-level files are named builtin-<module>,
    // so they are not merged with nodes using the module's name, in aircraft
    // and add-ons. The <module> child overrides the name, everywhere.
    std::string name = node->getNameString();
    if (simgear::strutils::starts_with(name, BuiltinPrefix)) {
        name = name.substr(BuiltinPrefix.size());
    }
    if (node->hasChild("module")) {
        name = node->getStringValue("module");
    }

    auto module = std::make_unique<NasalModule>(sys, name, node);

    // <file> entries naming files (or, ending in '/', directories) of the Nasal
    // directories are resolved here, and the module loads those instead of
    // scanning its directory. Any other entry is a path for runtime resolution
    // (aircraft or add-on files).
    for (const auto& entry : node->getChildren("file")) {
        const std::string relPath = entry->getStringValue();
        const bool isDir = simgear::strutils::ends_with(relPath, "/");
        bool found = false;
        for (const auto& ff : available) {
            if (isDir ? simgear::strutils::starts_with(ff.relPath, relPath) : (ff.relPath == relPath)) {
                module->addFile(ff.path);
                found = true;
            }
        }
        if (found) {
            module->_listed.insert(relPath);
        } else if (isDir) {
            SG_LOG(SG_NASAL, SG_DEV_ALERT, "Nasal module " << name << ": no files found in: " << relPath);
        }
    }

    return module;
}

NasalModule::NasalModule(FGNasalSys* sys, const std::string& name, SGPropertyNode* node)
    : _sys(sys),
      _name(name),
      _node(node)
{
}

bool NasalModule::setEnabledByDefault(bool value)
{
    if (!_node || _node->hasChild("enabled", 0)) {
        return false;
    }

    SGPropertyNode* enabled = _node->getChild("enabled", 0, true);
    enabled->setBoolValue(value);
    enabled->setAttribute(SGPropertyNode::USERARCHIVE, false);
    return true;
}

void NasalModule::watchEnabled()
{
    if (_listening || !_node) {
        return;
    }

    if (SGPropertyNode* enabled = _node->getChild("enabled")) {
        enabled->addChangeListener(this, false);
        _listening = true;
    }
}

NasalModule::~NasalModule()
{
    if (_listening && _node) {
        if (SGPropertyNode* enabled = _node->getChild("enabled")) {
            enabled->removeChangeListener(this);
        }
    }
}

bool NasalModule::isEnabled() const
{
    return _node ? _node->getBoolValue("enabled", true) : true;
}

bool NasalModule::isLoaded() const
{
    return _loaded;
}

void NasalModule::addFile(const SGPath& path)
{
    if (std::find(_files.begin(), _files.end(), path) == _files.end()) {
        _files.push_back(path);
    }
}

std::vector<std::string> NasalModule::extraFiles() const
{
    std::vector<std::string> result;
    if (!_node) {
        return result;
    }

    for (const auto& f : _node->getChildren("file")) {
        if (_listed.count(f->getStringValue()) == 0) {
            result.push_back(f->getStringValue());
        }
    }
    return result;
}

void NasalModule::setNativeHooks(std::function<void()> load, std::function<void()> unload)
{
    _loadHook = std::move(load);
    _unloadHook = std::move(unload);
}

void NasalModule::setPostLoadHook(std::function<void()> postLoad)
{
    _postLoadHook = std::move(postLoad);
}

void NasalModule::setLoaded(bool b)
{
    _loaded = b;
    if (!_node) {
        return;
    }

    SGPropertyNode* loaded = _node->getChild("loaded", 0, true);
    loaded->setAttribute(SGPropertyNode::PRESERVE, true);
    loaded->setBoolValue(b);
}

bool NasalModule::loadFile(const std::string& file)
{
    const SGPath p = resolveModuleFilePath(file);
    if (p.isNull()) {
        simgear::reportFailure(simgear::LoadFailure::NotFound, simgear::ErrorCode::AircraftSystems,
                               std::string{"Missing nasal file for module:"} + _name, sg_location{file});
        return false;
    }

    return runFile(p);
}

bool NasalModule::runFile(const SGPath& file)
{
    if (!file.exists()) {
        SG_LOG(SG_NASAL, SG_ALERT, "Cannot load module, missing file:" << file);
        return false;
    }

    SGMMapFile mmap(file);
    mmap.open(SG_IO_IN);

    const auto pathStr = file.utf8Str();
    return run(pathStr.c_str(), mmap.get(), mmap.get_size());
}

// Parse and run.  Save the local variables namespace, as it will become a
// sub-object of globals.
bool NasalModule::run(const char* fileName, const char* src, int len,
                      const SGPropertyNode* cmdarg, int argc, naRef* args,
                      int sourceLineOffset)
{
    auto d = _sys->d.get();
    if (naIsNil(d->_globals)) {
        return false;
    }

    naContext ctx = naNewContext();
    std::string errors;
    naRef code = _sys->parse(ctx, fileName, src, len, errors, sourceLineOffset);
    if (naIsNil(code)) {
        naFreeContext(ctx);
        return false;
    }

    // See if we already have a module hash to use.  This allows the
    // user to, for example, add functions to the built-in math
    // module.  Make a new one if necessary.
    naRef locals;
    naRef modname = naNewString(ctx);
    naStr_fromdata(modname, _name.c_str(), _name.size());

    if (!naHash_get(d->_globals, modname, &locals)) {
        // if the module was deleted, ensure the C++ pieces of a hybrid module
        // (e.g. canvas) are defined again first
        if (_loadHook && !_hookRun) {
            _loadHook();
            _hookRun = true;
        }

        if (!naHash_get(d->_globals, modname, &locals)) {
            locals = naNewHash(ctx);
        }
    }

    d->_cmdArg = (SGPropertyNode*)cmdarg;
    _sys->callWithContext(ctx, code, argc, args, locals);

    if (const char* error = naGetError(ctx)) {
        int line = naGetLine(ctx, 0);
        char* file = naStr_data(naGetSourceFile(ctx, 0));
        SG_LOG(SG_NASAL, SG_ALERT, "Error loading module '" << _name << "': " << error << " (at " << file << ":" << line << ")");
        naFreeContext(ctx);
        return false;
    }

    _sys->hashset(d->_globals, _name.c_str(), locals);

    naFreeContext(ctx);
    return true;
}

bool NasalModule::canReload() const
{
    // modules created entirely from a string (eg, dialogs or input XML)
    // cannot be reloaded for the moment.
    return !_files.empty() || !extraFiles().empty() || (_node && _node->hasChild("script"));
}

bool NasalModule::load()
{
    // the hook runs even when disabled, the native parts are always available
    _hookRun = true;
    _loadOk = true;
    if (_loadHook) {
        _loadHook();
    }

    if (!isEnabled()) {
        setLoaded(false);
        return false;
    }

    for (const auto& f : _files) {
        _loadOk &= loadFile(f.utf8Str());
    }

    const auto extras = extraFiles();
    for (const auto& f : extras) {
        _loadOk &= loadFile(f);
    }

    const std::string src = _node ? _node->getStringValue("script") : std::string{};
    if (!src.empty()) {
        _loadOk &= run(_node->getPath().c_str(), src.c_str(), src.length());
    }

    if (_node && _files.empty() && extras.empty() && src.empty()) {
        // module no longer exists - clear the archived "enable" flag
        _node->setAttribute(SGPropertyNode::USERARCHIVE, false);
        if (SGPropertyNode* enabled = _node->getChild("enabled", 0, false)) {
            enabled->setAttribute(SGPropertyNode::USERARCHIVE, false);
        }

        SG_LOG(SG_NASAL, SG_ALERT, "Nasal error: no <file> or <script> defined in /nasal/" << _name);
        setLoaded(false);
        return false;
    }

    setLoaded(_loadOk);
    if (_postLoadHook) {
        _postLoadHook();
    }
    return _loadOk;
}

void NasalModule::unload()
{
    // modules can use their 'loaded' value going false, to trigger unload
    // behaviours
    markUnloaded();

    auto d = _sys->d.get();
    if (d->_inited && !naIsNil(d->_globals)) {
        // (when not inited: can occur on shutdown due to us being shutdown
        // first, but other subsystems having Nasal objects)
        naContext ctx = naNewContext();
        naRef modname = naNewString(ctx);
        naStr_fromdata(modname, _name.c_str(), _name.size());

        naRef locals;
        if (naHash_get(d->_globals, modname, &locals)) {
            naRef unloadFunc = naHash_cget(locals, (char*)"unload");
            if (naIsFunc(unloadFunc)) {
                _sys->callWithContext(ctx, unloadFunc, 0, nullptr, locals);
            }

            // now delete the module hash
            naHash_delete(d->_globals, modname);
        }

        naFreeContext(ctx);
    }

    if (_unloadHook) {
        _unloadHook();
    }
}

void NasalModule::markUnloaded()
{
    _hookRun = false;
    setLoaded(false);
}

bool NasalModule::checkFiles() const
{
    auto check = [this](const std::string& file) {
        const SGPath p = resolveModuleFilePath(file);
        if (p.isNull() || !_sys->checkSyntax(p)) {
            SG_LOG(SG_NASAL, SG_ALERT, "Nasal module '" << _name << "': problem with file:" << file);
            return false;
        }
        return true;
    };

    bool ok = true;
    for (const auto& f : _files) {
        ok &= check(f.utf8Str());
    }
    for (const auto& f : extraFiles()) {
        ok &= check(f);
    }
    return ok;
}

void NasalModule::valueChanged(SGPropertyNode*)
{
    const bool enabled = isEnabled();
    SG_LOG(SG_NASAL, SG_INFO, "Nasal module " << _name << ": 'enabled' changed to " << enabled << " (loaded: " << _loaded << ")");

    if (enabled && !_loaded) {
        SG_LOG(SG_NASAL, SG_INFO, "Nasal module " << _name << ": loading");
        load();
    } else if (!enabled && _loaded) {
        SG_LOG(SG_NASAL, SG_INFO, "Nasal module " << _name << ": unloading");
        unload();
    } else {
        SG_LOG(SG_NASAL, SG_INFO, "Nasal module " << _name << ": nothing to do");
    }
}
