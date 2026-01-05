#pragma once
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

#include "SwObject.h"
#include "SwFont.h"

#include "Sw.h"

class MouseEvent;
class KeyEvent;
class WheelEvent;
class PaintEvent;
class StyleSheet;

class SwWidgetInterface : public SwObject {

    VIRTUAL_PROPERTY(SwFont, Font)
public:
    // Constructeur et destructeur
    SwWidgetInterface(SwObject* parent = nullptr) : SwObject(parent) {}
    virtual ~SwWidgetInterface() = default;

    // Méthodes purement virtuelles pour définir les fonctionnalités d'un widget
    virtual void show() = 0;
    virtual void hide() = 0;
    virtual void update() = 0;
    virtual void move(int newX, int newY) = 0;
    virtual void resize(int newWidth, int newHeight) = 0;

    // Méthodes pour gérer les événements
    virtual void paintEvent(PaintEvent* event) = 0;
    virtual void mousePressEvent(MouseEvent* event) = 0;
    virtual void mouseReleaseEvent(MouseEvent* event) = 0;
    virtual void mouseDoubleClickEvent(MouseEvent* event) = 0;
    virtual void mouseMoveEvent(MouseEvent* event) = 0;
    virtual void keyPressEvent(KeyEvent* event) = 0;
    virtual void wheelEvent(WheelEvent* event) { SW_UNUSED(event); }

    // Méthodes pour obtenir ou définir des propriétés générales
    virtual StyleSheet* getToolSheet() = 0;
    virtual SwRect getRect() const = 0;

    virtual SwRect sizeHint() const {
        return SwRect{0, 0, 0, 0};
    }

    virtual SwRect minimumSizeHint() const {
        return SwRect{0, 0, 0, 0};
    }

//    virtual bool isVisible() const = 0;
//    virtual int getWidth() const = 0;
//    virtual int getHeight() const = 0;
//    virtual int getX() const = 0;
//    virtual int getY() const = 0;
//    virtual void setWidth(int width) = 0;
//    virtual void setHeight(int height) = 0;
//    virtual void setX(int x) = 0;
//    virtual void setY(int y) = 0;
};
