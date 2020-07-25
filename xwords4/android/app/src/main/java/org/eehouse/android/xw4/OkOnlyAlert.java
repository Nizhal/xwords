/* -*- compile-command: "find-and-gradle.sh inXw4dDeb"; -*- */
/*
 * Copyright 2017 - 2020 by Eric House (xwords@eehouse.org).  All rights
 * reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License as
 * published by the Free Software Foundation; either version 2 of the
 * License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
 */

package org.eehouse.android.xw4;

import android.app.AlertDialog;
import android.content.Context;

import org.eehouse.android.xw4.DlgDelegate.ActionPair;

public class OkOnlyAlert extends DlgDelegateAlert {
    private static final String TAG = OkOnlyAlert.class.getSimpleName();

    public static OkOnlyAlert newInstance( DlgState state )
    {
        OkOnlyAlert result = new OkOnlyAlert();
        result.addStateArgument( state );
        return result;
    }

    public OkOnlyAlert() {}

    @Override
    public void populateBuilder( Context context, DlgState state,
                                 AlertDialog.Builder builder )
    {
        builder.setMessage( state.m_msg )
            .setPositiveButton( state.m_posButton, null )
            ;

        ActionPair pair = state.m_pair;
        if ( null != pair ) {
            builder.setNeutralButton( pair.buttonStr, mkCallbackClickListener( pair ) );
        }
    }
}
