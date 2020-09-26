/* -*- compile-command: "find-and-gradle.sh inXw4dDeb"; -*- */
/*
 * Copyright 2020 by Eric House (xwords@eehouse.org).  All rights reserved.
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

import android.content.Context;
import android.util.AttributeSet;
import android.view.View;
import android.widget.RadioButton;
import android.widget.RadioGroup;
import android.widget.ScrollView;

import java.util.HashMap;
import java.util.List;
import java.util.Map;

import org.eehouse.android.xw4.DlgDelegate.DlgClickNotify.InviteMeans;
import org.eehouse.android.xw4.jni.CommsAddrRec.CommsConnType;
import org.eehouse.android.xw4.loc.LocUtils;

public class InviteView extends ScrollView
    implements RadioGroup.OnCheckedChangeListener {

    private static final String TAG = InviteView.class.getSimpleName();
    
    public interface ItemClicked {
        public void meansClicked( InviteMeans means );
    }

    private ItemClicked mProcs;
    private boolean mIsWho;
    private RadioGroup mGroupTab;
    private RadioGroup mGroupWho;
    private RadioGroup mGroupHow;
    private Map<RadioButton, InviteMeans> mHowMeans = new HashMap<>();
    private Map<RadioButton, String> mWhoPlayers = new HashMap<>();

    public InviteView( Context context, AttributeSet as ) {
        super( context, as );
    }

    public InviteView setChoices( List<InviteMeans> meansList, int sel,
                                  String[] players )
    {
        Context context = getContext();

        mIsWho = null != players && 0 < players.length;

        // top/horizontal group first
        mGroupTab = (RadioGroup)findViewById( R.id.group_tab );
        mGroupTab.check(mIsWho ? R.id.radio_who : R.id.radio_how );
        mGroupTab.setOnCheckedChangeListener( this );

        mGroupHow = (RadioGroup)findViewById( R.id.group_how );
        mGroupHow.setOnCheckedChangeListener( this );
        for ( InviteMeans means : meansList ) {
            Assert.assertNotNull( means );
            RadioButton button = new RadioButton( context );
            button.setText( LocUtils.getString( context, means.getUserDescID() ) );
            mGroupHow.addView( button );
            mHowMeans.put( button, means );
        }

        mGroupWho = (RadioGroup)findViewById( R.id.group_who );
        mGroupWho.setOnCheckedChangeListener( this );
        if ( mIsWho ) {
            for ( String player : players ) {
                RadioButton button = new RadioButton( context );
                button.setText( player );
                mGroupWho.addView( button );
                mWhoPlayers.put( button, player );
            }
        }
        showWhoOrHow();

        return this;
    }

    public InviteView setCallbacks( ItemClicked procs ) {
        mProcs = procs;
        return this;
    }

    public Object getChoice()
    {
        Object result = null;
        RadioButton checked = getCurCheckedFor();
        if ( null != checked ) {
            // result = new InviteChoice();
            if ( mIsWho ) {
                result = mWhoPlayers.get(checked);
            } else {
                result = mHowMeans.get(checked);
            }
        }
        Log.d( TAG, "getChoice() => %s", result );
        return result;
    }

    @Override
    public void onCheckedChanged( RadioGroup group, int checkedId )
    {
        if ( -1 != checkedId ) {
            switch( group.getId() ) {
            case R.id.group_tab:
                mIsWho = checkedId == R.id.radio_who;
                showWhoOrHow();
                break;
            case R.id.group_how:
                RadioButton button = (RadioButton)group.findViewById(checkedId);
                InviteMeans means = mHowMeans.get( button );
                mProcs.meansClicked( means );
                break;
            case R.id.group_who:
                break;
            }
        }
    }

    private RadioButton getCurCheckedFor()
    {
        RadioButton result = null;
        RadioGroup group = mIsWho ? mGroupWho : mGroupHow;
        int curSel = group.getCheckedRadioButtonId();
        if ( 0 <= curSel ) {
            result = (RadioButton)findViewById(curSel);
        }
        return result;
    }

    private void showWhoOrHow()
    {
        mGroupWho.setVisibility( mIsWho ? View.VISIBLE : View.INVISIBLE );
        mGroupHow.setVisibility( mIsWho ? View.INVISIBLE : View.VISIBLE );

        boolean showEmpty = mIsWho && 0 == mWhoPlayers.size();
        findViewById( R.id.who_empty )
            .setVisibility( showEmpty ? View.VISIBLE : View.INVISIBLE );
    }
}
