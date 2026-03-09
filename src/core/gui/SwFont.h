
/**
 * @file
 * @ingroup core_gui
 * @brief Declares `SwFont`, the value type that describes widget and painter fonts.
 *
 * The class stores family, point size, weight, and style flags in a backend-neutral form
 * while lazily creating native handles when a platform painter needs them. It acts as the
 * portable font contract shared by widgets, styles, and text measurement helpers.
 */

/***************************************************************************************************
 * This file is part of a project developed by Eymeric O'Neill.
 *
 * Copyright (C) 2025 Ariya Consulting
 * Author/Creator: Eymeric O'Neill
 * Contact: +33 6 52 83 83 31
 * Email: eymeric.oneill@gmail.com
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 ***************************************************************************************************/

#pragma once




#include <string>
#include <iostream>
#include "SwDebug.h"
#include "Sw.h"
static constexpr const char* kSwLogCategory_SwFont = "sw.core.gui.swfont";


#if defined(_WIN32)
#include "platform/win/SwWindows.h"
#else
using HFONT = void *;
using HDC = void *;
#endif

/**
 * @brief Value object describing a concrete font face and style selection.
 *
 * The class is shared by widgets, dialogs, painters, and text metrics helpers.
 * It keeps font state independent from any specific painter instance while still
 * allowing platform-specific handles to be materialized on demand.
 */
class SwFont
{
public:
    /**
     * @brief Constructs a `SwFont` instance.
     * @param family Value passed to the method.
     * @param pointSize Value passed to the method.
     * @param weight Value passed to the method.
     * @param italic Value passed to the method.
     * @param underline Value passed to the method.
     * @param underline Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwFont(const std::wstring &family = L"Segoe UI",
           int pointSize = 9,
           FontWeight weight = Normal,
           bool italic = false,
           bool underline = false)
        : familyName(family), pointSize(pointSize), weight(weight), italic(italic), underline(underline)
    {
    }

    /**
     * @brief Constructs a `SwFont` instance.
     * @param underline Value passed to the method.
     *
     * @details The instance is initialized and prepared for immediate use.
     */
    SwFont(const SwFont &other)
        : familyName(other.familyName),
          pointSize(other.pointSize),
          weight(other.weight),
          italic(other.italic),
          underline(other.underline)
    {
    }

    /**
     * @brief Performs the `operator==` operation.
     * @param other Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator==(const SwFont &other) const
    {
        return familyName == other.familyName && pointSize == other.pointSize && weight == other.weight &&
               italic == other.italic && underline == other.underline;
    }

    /**
     * @brief Performs the `&operator=` operation.
     * @param other Value passed to the method.
     * @return The requested &operator =.
     */
    SwFont &operator=(const SwFont &other)
    {
        if (this != &other)
        {
            familyName = other.familyName;
            pointSize = other.pointSize;
            weight = other.weight;
            italic = other.italic;
            underline = other.underline;
            m_dirty = true;
        }
        return *this;
    }

    /**
     * @brief Destroys the `SwFont` instance.
     *
     * @details Use this hook to release any resources that remain associated with the instance.
     */
    ~SwFont()
    {
#if defined(_WIN32)
        releaseFontHandle_();
#endif
    }

    /**
     * @brief Sets the family.
     * @param family Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setFamily(const std::wstring &family)
    {
        if (familyName != family)
        {
            familyName = family;
            m_dirty = true;
        }
    }

    /**
     * @brief Returns the current family.
     * @return The current family.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    std::wstring getFamily() const { return familyName; }

    /**
     * @brief Sets the point Size.
     * @param size Size value used by the operation.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setPointSize(int size)
    {
        if (pointSize != size)
        {
            pointSize = size;
            m_dirty = true;
        }
    }

    /**
     * @brief Returns the current point Size.
     * @return The current point Size.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    int getPointSize() const { return pointSize; }

    /**
     * @brief Sets the weight.
     * @param fontWeight Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setWeight(FontWeight fontWeight)
    {
        if (weight != fontWeight)
        {
            weight = fontWeight;
            m_dirty = true;
        }
    }

    /**
     * @brief Returns the current weight.
     * @return The current weight.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    FontWeight getWeight() const { return weight; }

    /**
     * @brief Sets the italic.
     * @param isItalic Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setItalic(bool isItalic)
    {
        if (italic != isItalic)
        {
            italic = isItalic;
            m_dirty = true;
        }
    }

    /**
     * @brief Returns whether the object reports italic.
     * @return `true` when the object reports italic; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isItalic() const { return italic; }

    /**
     * @brief Sets the underline.
     * @param isUnderline Value passed to the method.
     *
     * @details Call this method to replace the currently stored value with the caller-provided one.
     */
    void setUnderline(bool isUnderline)
    {
        if (underline != isUnderline)
        {
            underline = isUnderline;
            m_dirty = true;
        }
    }

    /**
     * @brief Returns whether the object reports underline.
     * @return `true` when the object reports underline; otherwise `false`.
     *
     * @details The returned value reflects the state currently stored by the instance.
     */
    bool isUnderline() const { return underline; }

    /**
     * @brief Performs the `operator!=` operation.
     * @param this Value passed to the method.
     * @return `true` on success; otherwise `false`.
     */
    bool operator!=(const SwFont &other) const { return !(*this == other); }

    HFONT handle(HDC context)
    {
#if defined(_WIN32)
        updateFontHandle(context);
        return hFont;
#else
        (void)context;
        return nullptr;
#endif
    }

private:
    std::wstring familyName;
    int pointSize;
    FontWeight weight;
    bool italic;
    bool underline;
    HFONT hFont = nullptr;
    bool m_ownsFontHandle{false};
    int m_cachedDpiY{0};
    bool m_dirty{true};

    void releaseFontHandle_()
    {
#if defined(_WIN32)
        if (hFont && m_ownsFontHandle)
        {
            DeleteObject(hFont);
        }
        hFont = nullptr;
        m_ownsFontHandle = false;
        m_cachedDpiY = 0;
#endif
    }

    void updateFontHandle(HDC context)
    {
#if defined(_WIN32)
        HDC dc = context;
        bool releaseDc = false;
        if (!dc)
        {
            dc = GetDC(nullptr);
            releaseDc = (dc != nullptr);
        }

        const int dpiY = dc ? GetDeviceCaps(dc, LOGPIXELSY) : 96;
        if (!m_dirty && hFont && dpiY == m_cachedDpiY)
        {
            if (releaseDc && dc)
            {
                ReleaseDC(nullptr, dc);
            }
            return;
        }

        releaseFontHandle_();

        std::wstring fam = familyName.empty() ? std::wstring(L"Segoe UI") : familyName;
        const int size = pointSize > 0 ? pointSize : 9;
        const int pixelHeight = -MulDiv(size, dpiY, 72);

        hFont = CreateFontW(
            pixelHeight,
            0,
            0,
            0,
            weight,
            italic,
            underline,
            FALSE,
            DEFAULT_CHARSET,
            OUT_DEFAULT_PRECIS,
            CLIP_DEFAULT_PRECIS,
            DEFAULT_QUALITY,
            DEFAULT_PITCH | FF_DONTCARE,
            fam.c_str());

        if (!hFont)
        {
            static bool warned = false;
            if (!warned)
            {
                swCWarning(kSwLogCategory_SwFont) << "Erreur lors de la création de la police, utilisation de la police par défaut.";
                warned = true;
            }
            hFont = (HFONT)GetStockObject(DEFAULT_GUI_FONT);
            m_ownsFontHandle = false;
        }
        else
        {
            m_ownsFontHandle = true;
        }

        m_cachedDpiY = dpiY;
        m_dirty = false;

        if (releaseDc && dc)
        {
            ReleaseDC(nullptr, dc);
        }
#else
        (void)context;
#endif
    }
};
