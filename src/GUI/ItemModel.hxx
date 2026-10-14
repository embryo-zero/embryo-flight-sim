// SPDX-License-Identifier: GPL-2.0-or-later
// SPDX-FileCopyrightText: 2025 James Turner <james@flightgear.org>

/**
 * @file
 * @brief Abstract models for the GUI, similar to QAbstractList Model
 */

#pragma once

#include <any>
#include <functional>
#include <map>
#include <memory>
#include <simgear/props/props.hxx>
#include <string>
#include <vector>

namespace nasal {
class Hash;
}

class ItemModel : virtual public SGVirtualWeakReferenced
{
public:
    virtual ~ItemModel() = default;

    std::string labelAt(size_t index) const;
    std::any valueAt(size_t index) const;

    virtual size_t count() const = 0;
    virtual std::any dataAt(size_t index, const std::string& key) const = 0;

    enum class Change {
        Reset,
        Modified,
        RowsWillBeAdded,
        RowsAdded,
        RowsWillBeRemoved,
        RowsRemoved
    };

    using ChangeCallback = std::function<void(Change t, size_t row, size_t count)>;

    /**
     * @brief register a callback for model changes
     * @return a stable identifier, to pass to removeChangeCallback. Identifiers
     * are never reused, so removing one callback does not invalidate others.
     */
    size_t addChangeCallback(ChangeCallback cb);
    void removeChangeCallback(size_t callbackId);

    /**
     * Nasal-facing variant of addChangeCallback. The callback receives the
     * change type as an integer matching the gui.MODEL_CHANGE_* constants
     * (the values of ItemModel::Change), the first row and the row count.
     * Remove with removeChangeCallback.
     */
    size_t addNasalChangeListener(std::function<void(int, int, int)> f);

    static void setupGhosts(nasal::Hash& ns);

protected:
    // helpers for implementation classes to use, to generate
    // change callback events

    void didChangeData(int row, int count);
    void didReset();

    void beginAddRows(int row, int count);
    void endAddRows();

    void beginRemoveRows(int row, int count);
    void endRemoveRows();

private:
    void fireCallbacks(Change t, size_t row, size_t count = 1);

    bool m_addRemoveActive = false;
    size_t m_addRemoveRow = 0,
           m_addRemoveCount = 0;
    size_t m_nextCallbackId = 1;
    std::map<size_t, ChangeCallback> m_callbacks; // ordered by id, so fired in registration order
    mutable size_t m_cachedCount = 0;
};

using ItemModelRef = SGSharedPtr<ItemModel>;

class PropertyItemModel : public ItemModel
{
public:
    PropertyItemModel(SGPropertyNode_ptr root);
    virtual ~PropertyItemModel();

    void setItemName(const std::string& s);
    void setLabelPath(const std::string& s);
    void setValuePath(const std::string& s);

    /**
     * Enable translation of labels. When enabled (and a translation resource
     * is set), a label is translated if its property carries the 'translate'
     * attribute, mirroring the behaviour of the XML dialog system. The
     * untranslated label is always available via the 'labelKey' data key.
     */
    void setAutoTranslate(bool b);
    bool autoTranslate() const;

    /**
     * Translation resource (aka context) to look labels up in, eg
     * "dialog-airports". Translation is skipped if this is empty.
     */
    void setTranslationResource(const std::string& resource);
    std::string translationResource() const;

    /**
     * Translation domain, eg "core" (the default) or "current-aircraft"
     */
    void setTranslationDomain(const std::string& domain);
    std::string translationDomain() const;

    size_t count() const override;
    std::any dataAt(size_t index, const std::string& key) const override;

private:
    class PropertyItemModelPrivate;
    friend class PropertyItemModelPrivate;
    std::unique_ptr<PropertyItemModelPrivate> d;
};
