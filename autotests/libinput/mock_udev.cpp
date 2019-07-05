/********************************************************************
KWin - the KDE window manager
This file is part of the KDE project.

Copyright (C) 2016 Martin Gräßlin <mgraesslin@kde.org>

This program is free software; you can redistribute it and/or modify
it under the terms of the GNU General Public License as published by
the Free Software Foundation; either version 2 of the License, or
(at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program.  If not, see <http://www.gnu.org/licenses/>.
*********************************************************************/

#include "mock_udev.h"

#include "../toolkit/udev_context.h"

udev *udev::s_mockUdev = nullptr;

namespace KWin
{

UdevContext::UdevContext()
    : m_udev(udev::s_mockUdev)
{
}

UdevContext::UdevContext(const UdevContext &other) = default;
UdevContext::UdevContext(UdevContext &&other) = default;
UdevContext::~UdevContext() = default;

UdevContext &UdevContext::operator=(const UdevContext &other) = default;
UdevContext &UdevContext::operator=(UdevContext &&other) = default;

UdevContext::operator udev*() const
{
    return m_udev;
}

}
