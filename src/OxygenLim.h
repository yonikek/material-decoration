/*
 * Copyright (C) 2020 Chris Holland <zrenfire@gmail.com>
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

// Qt
#include <QLoggingCategory>


namespace OxygenLim
{
      
    static const QLoggingCategory category("kdecoration.oxygenlim");
    static const QString s_configFilename = QStringLiteral("oxygen-decoration-limrc");
    static constexpr qreal cornerRadiusAdjustment = 0.7;

    // Oxygen metrics (in units of the small/large spacing, or pixels)
    namespace Metrics
    {
        static constexpr qreal CornerRadius = 3.5;
        static constexpr int TitleBar_TopMargin = 2;
        static constexpr int TitleBar_BottomMargin = 2;
        static constexpr int TitleBar_SideMargin = 2;
        static constexpr int TitleBar_ButtonSpacing = 2;
    }
    
    
    //--- Standard pen widths
    namespace PenWidth
    {
        /* https://github.com/KDE/breeze/blob/master/kstyle/breeze.h#L164
         * Using 1 instead of slightly more than 1 causes symbols drawn with
         * pen strokes to look skewed. The exact amount added does not matter
         * as long as it isn't too visible.
         */
        // The standard pen stroke width for symbols.
        static constexpr qreal Symbol = 1.01;
    }

} // namespace OxygenLim
