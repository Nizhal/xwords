/* -*- compile-command: "find-and-gradle.sh inXw4dDeb"; -*- */
/*
 * Copyright 2009-2010 by Eric House (xwords@eehouse.org).  All
 * rights reserved.
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

import android.app.Activity;
import android.app.Dialog;
import android.content.Intent;
import android.os.Bundle;
import android.preference.PreferenceActivity;

import org.eehouse.android.xw4.DlgDelegate.Action;
import org.eehouse.android.xw4.DlgDelegate.Builder;
import org.eehouse.android.xw4.loc.LocUtils;

public class PrefsActivity extends PreferenceActivity
    implements Delegator, DlgDelegate.HasDlgDelegate {

    private PrefsDelegate m_dlgt;

    @Override
    protected void onCreate( Bundle savedInstanceState )
    {
        super.onCreate( savedInstanceState );
        m_dlgt = new PrefsDelegate( this, this, savedInstanceState );

        int layoutID = m_dlgt.getLayoutID();
        if ( 0 < layoutID ) {
            m_dlgt.setContentView( layoutID );
        }

        m_dlgt.init( savedInstanceState );
    }

    @Override
    protected void onStart()
    {
        LocUtils.xlatePreferences( this );
        super.onStart();
        m_dlgt.onStart();
    }

    @Override
    protected void onResume()
    {
        super.onResume();
        m_dlgt.onResume();
   }

    @Override
    protected void onPause()
    {
        super.onPause();
        m_dlgt.onPause();
    }

    @Override
    protected void onStop()
    {
        m_dlgt.onStop();
        super.onStop();
    }

    @Override
    protected void onDestroy()
    {
        m_dlgt.onDestroy();
        super.onDestroy();
    }

    @Override
    protected Dialog onCreateDialog( int id )
    {
        return m_dlgt.onCreateDialog( id );
    }

    @Override
    protected void onActivityResult( int requestCode, int resultCode,
                                     Intent data )
    {
        RequestCode rc = RequestCode.values()[requestCode];
        m_dlgt.onActivityResult( rc, resultCode, data );
    }

    public Builder makeOkOnlyBuilder( int msgID )
    {
        return m_dlgt.makeOkOnlyBuilder( msgID );
    }

    public Builder makeOkOnlyBuilder( String msg )
    {
        return m_dlgt.makeOkOnlyBuilder( msg );
    }

    public Builder makeNotAgainBuilder(int msgID, int key, Action action)
    {
        return m_dlgt.makeNotAgainBuilder( msgID, key, action );
    }

    public Builder makeNotAgainBuilder( int msgID, int key )
    {
        return m_dlgt.makeNotAgainBuilder( msgID, key );
    }

    public Builder makeConfirmThenBuilder( String msg, Action action )
    {
        return m_dlgt.makeConfirmThenBuilder( msg, action );
    }

    public Builder makeConfirmThenBuilder( int msgID, Action action )
    {
        return m_dlgt.makeConfirmThenBuilder( msgID, action );
    }

    protected void showSMSEnableDialog( Action action )
    {
        m_dlgt.showSMSEnableDialog( action );
    }

    //////////////////////////////////////////////////////////////////////
    // Delegator interface
    //////////////////////////////////////////////////////////////////////
    public Activity getActivity()
    {
        return this;
    }

    public Bundle getArguments()
    {
        return getIntent().getExtras();
    }

    public boolean inDPMode() { Assert.failDbg(); return false; }
    public void addFragment( XWFragment fragment, Bundle extras ) { Assert.failDbg(); }
    public void addFragmentForResult( XWFragment fragment, Bundle extras,
                                      RequestCode code ) { Assert.failDbg(); }
}
