/***
*
*	Copyright (c) 1996-2001, Valve LLC. All rights reserved.
*	
*	This product contains software technology licensed from Id 
*	Software, Inc. ("Id Technology").  Id Technology (c) 1996 Id Software, Inc. 
*	All Rights Reserved.
*
*   Use, distribution, and modification of this source code and/or resulting
*   object code is restricted to non-commercial enhancements to products from
*   Valve LLC.  All other use, distribution, or modification is prohibited
*   without written permission from Valve LLC.
*
****/
#if !defined( OEM_BUILD ) && !defined( HLDEMO_BUILD )

#include "extdll.h"
#include "util.h"
#include "cbase.h"
#include "monsters.h"
#include "weapons.h"
#include "nodes.h"
#include "player.h"
#include "soundent.h"
#include "shake.h"
#include "gamerules.h"
#include "xenspit.h"


#define	GAUSS_PRIMARY_CHARGE_VOLUME	256// how loud gauss is while charging
#define GAUSS_PRIMARY_FIRE_VOLUME	450// how loud gauss is when discharged

#define FIRE2_SEQUENCE_DURATION		(33.0f / 15.0f)

enum gauss_e {
	GAUSS_IDLE = 0,
	GAUSS_IDLE2,
	GAUSS_FIDGET,
	GAUSS_SPINUP,
	GAUSS_SPIN,
	GAUSS_FIRE,
	GAUSS_FIRE2,
	GAUSS_HOLSTER,
	GAUSS_DRAW,
	GAUSS_RELOAD,
};

LINK_ENTITY_TO_CLASS( weapon_xs, CGauss );

float CGauss::GetFullChargeTime( void )
{
#ifdef CLIENT_DLL
	if ( bIsMultiplayer() )
#else
	if ( g_pGameRules->IsMultiplayer() )
#endif
	{
		return 1.5;
	}

	return 4;
}

#ifdef CLIENT_DLL
extern int g_irunninggausspred;
#endif

void CGauss::Spawn( )
{
	Precache( );
	m_iId = WEAPON_GAUSS;
	SET_MODEL(ENT(pev), "models/w_xs.mdl");

	m_iDefaultAmmo = GAUSS_DEFAULT_GIVE;

	m_iNumConsumedAmmo = 0; // Reset used ammo.

	FallInit();// get ready to fall down.

	Materialize();
	SetThink( &CGauss::IdleThink ); // Play idle sequence in think function.

	// Reset sequence.
	pev->sequence = 0;
	ResetSequenceInfo();

	pev->nextthink = gpGlobals->time + 0.01f;
}


void CGauss::Precache( void )
{
	PRECACHE_MODEL("models/w_xs.mdl");
	PRECACHE_MODEL("models/v_xs.mdl");
	PRECACHE_MODEL("models/p_xs.mdl");

	PRECACHE_SOUND("items/9mmclip1.wav");

	PRECACHE_SOUND("weapons/xs_moan1.wav");
	PRECACHE_SOUND("weapons/xs_moan2.wav");
	PRECACHE_SOUND("weapons/xs_moan3.wav");
	PRECACHE_SOUND("weapons/xs_reload.wav");
	PRECACHE_SOUND("weapons/xs_shot.wav");
	PRECACHE_SOUND("weapons/xs_windup.wav");
	
	m_iGlow = PRECACHE_MODEL( "sprites/hotglow.spr" );
	m_iBalls = PRECACHE_MODEL( "sprites/hotglow.spr" );
	m_iBeam = PRECACHE_MODEL( "sprites/smoke.spr" );

	m_usGaussFire = PRECACHE_EVENT( 1, "events/gauss.sc" );
	m_usGaussSpin = PRECACHE_EVENT( 1, "events/gaussspin.sc" );

	PRECACHE_MODEL("sprites/glow02.spr");

	UTIL_PrecacheOther("xs_beam");
}

int CGauss::AddToPlayer( CBasePlayer *pPlayer )
{
	if ( CBasePlayerWeapon::AddToPlayer( pPlayer ) )
	{
		MESSAGE_BEGIN( MSG_ONE, gmsgWeapPickup, NULL, pPlayer->pev );
			WRITE_BYTE( m_iId );
		MESSAGE_END();
		return TRUE;
	}
	return FALSE;
}

int CGauss::GetItemInfo(ItemInfo *p)
{
	p->pszName = STRING(pev->classname);
	p->pszAmmo1 = "uranium";
	p->iMaxAmmo1 = URANIUM_MAX_CARRY;
	p->pszAmmo2 = NULL;
	p->iMaxAmmo2 = -1;
	p->iMaxClip = GAUSS_MAX_CLIP;
	p->iSlot = 3;
	p->iPosition = 0;
	p->iId = m_iId = WEAPON_GAUSS;
	p->iFlags = 0;
	p->iWeight = GAUSS_WEIGHT;

	return 1;
}

BOOL CGauss::Deploy( )
{
	m_pPlayer->m_flPlayAftershock = 0.0;
	return DefaultDeploy( "models/v_xs.mdl", "models/p_xs.mdl", GAUSS_DRAW, "gauss" );
}

void CGauss::Holster( int skiplocal /* = 0 */ )
{
	m_fInReload = FALSE;

	PLAYBACK_EVENT_FULL( FEV_RELIABLE | FEV_GLOBAL, m_pPlayer->edict(), m_usGaussFire, 0.01, (float *)&m_pPlayer->pev->origin, (float *)&m_pPlayer->pev->angles, 0.0, 0.0, 0, 0, 0, 1 );
	
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5;
	
	SendWeaponAnim( GAUSS_HOLSTER );
	m_fInAttack = 0;

	m_iNumConsumedAmmo = 0; // Reset used ammo.
}


void CGauss::PrimaryAttack()
{
	// don't fire underwater
	if ( m_pPlayer->pev->waterlevel == 3 )
	{
		PlayEmptySound( );
		m_flNextSecondaryAttack = m_flNextPrimaryAttack = GetNextAttackDelay(0.15);
		return;
	}

	if ( m_iClip <= 0 )
	{
		PlayEmptySound( );
		m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5;
		return;
	}

	m_pPlayer->m_iWeaponVolume = GAUSS_PRIMARY_FIRE_VOLUME;
	m_fPrimaryFire = TRUE;

	m_iClip--;

	StartFire();
	m_fInAttack = 0;
	m_iNumConsumedAmmo = 0;
	m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + FIRE2_SEQUENCE_DURATION;
	m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.4;
}

void CGauss::SecondaryAttack()
{
	// don't fire underwater
	if ( m_pPlayer->pev->waterlevel == 3 )
	{
		if ( m_fInAttack != 0 )
		{
			EMIT_SOUND_DYN(ENT(m_pPlayer->pev), CHAN_WEAPON, "weapons/xs_moan2.wav", 1.0, ATTN_NORM, 0, 80 + RANDOM_LONG(0,0x3f));
			SendWeaponAnim( GAUSS_IDLE );
			m_fInAttack = 0;
			m_iNumConsumedAmmo = 0;
		}
		else
		{
			PlayEmptySound( );
		}

		m_flNextSecondaryAttack = m_flNextPrimaryAttack = GetNextAttackDelay(0.5);
		return;
	}

	if ( m_fInAttack == 0 )
	{
		if ( m_iClip <= 0 )
		{
			EMIT_SOUND(ENT(m_pPlayer->pev), CHAN_WEAPON, "weapons/357_cock1.wav", 0.8, ATTN_NORM);
			m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 0.5;
			return;
		}

		m_fPrimaryFire = FALSE;

		m_iClip--;// take one ammo just to start the spin
		m_iNumConsumedAmmo++; // Increment num consumed ammo.
		m_pPlayer->m_flNextAmmoBurn = UTIL_WeaponTimeBase();

		// spin up
		m_pPlayer->m_iWeaponVolume = GAUSS_PRIMARY_CHARGE_VOLUME;
		
		SendWeaponAnim( GAUSS_SPINUP );
		m_fInAttack = 1;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.5;
		m_pPlayer->m_flStartCharge = gpGlobals->time;
		m_pPlayer->m_flAmmoStartCharge = UTIL_WeaponTimeBase() + GetFullChargeTime();

		PLAYBACK_EVENT_FULL( FEV_NOTHOST, m_pPlayer->edict(), m_usGaussSpin, 0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0.0, 0.0, 110, 0, 0, 0 );

		m_iSoundState = SND_CHANGE_PITCH;
	}
	else if (m_fInAttack == 1)
	{
		if (m_flTimeWeaponIdle <= UTIL_WeaponTimeBase())
		{
			SendWeaponAnim( GAUSS_SPIN );
			m_fInAttack = 2;
		}
	}
	else
	{
		// during the charging process, eat one bit of ammo every once in a while
		if ( UTIL_WeaponTimeBase() >= m_pPlayer->m_flNextAmmoBurn && m_pPlayer->m_flNextAmmoBurn != 1000 )
		{
#ifdef CLIENT_DLL
	if ( bIsMultiplayer() )
#else
	if ( g_pGameRules->IsMultiplayer() )
#endif
			{
				m_iClip--;
				m_iNumConsumedAmmo++;
				m_pPlayer->m_flNextAmmoBurn = UTIL_WeaponTimeBase() + 0.1;
			}
			else
			{
				m_iClip--;
				m_iNumConsumedAmmo++;
				m_pPlayer->m_flNextAmmoBurn = UTIL_WeaponTimeBase() + 0.3;
			}
		}

		if ( m_iClip <= 0 )
		{
			// out of ammo! force the gun to fire
			StartFire();
			m_fInAttack = 0;
			m_iNumConsumedAmmo = 0;
			m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + FIRE2_SEQUENCE_DURATION;
			m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 1;
			return;
		}
		
		if ( UTIL_WeaponTimeBase() >= m_pPlayer->m_flAmmoStartCharge )
		{
			// don't eat any more ammo after gun is fully charged.
			m_pPlayer->m_flNextAmmoBurn = 1000;
		}

		int pitch = ( gpGlobals->time - m_pPlayer->m_flStartCharge ) * ( 150 / GetFullChargeTime() ) + 100;
		if ( pitch > 250 ) 
			 pitch = 250;
		
		// ALERT( at_console, "%d %d %d\n", m_fInAttack, m_iSoundState, pitch );

		if ( m_iSoundState == 0 )
			ALERT( at_console, "sound state %d\n", m_iSoundState );

		PLAYBACK_EVENT_FULL( FEV_NOTHOST, m_pPlayer->edict(), m_usGaussSpin, 0.0, (float *)&g_vecZero, (float *)&g_vecZero, 0.0, 0.0, pitch, 0, ( m_iSoundState == SND_CHANGE_PITCH ) ? 1 : 0, 0 );

		m_iSoundState = SND_CHANGE_PITCH; // hack for going through level transitions

		m_pPlayer->m_iWeaponVolume = GAUSS_PRIMARY_CHARGE_VOLUME;
		
		// m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 0.1;
		if ( m_pPlayer->m_flStartCharge < gpGlobals->time - 10 )
		{
			// Player charged up too long. Zap him.
			EMIT_SOUND_DYN(ENT(m_pPlayer->pev), CHAN_WEAPON, "weapons/xs_moan1.wav", 1.0, ATTN_NORM, 0, 80 + RANDOM_LONG(0,0x3f));
			EMIT_SOUND_DYN(ENT(m_pPlayer->pev), CHAN_ITEM,   "weapons/xs_moan3.wav", 1.0, ATTN_NORM, 0, 75 + RANDOM_LONG(0,0x3f));
			
			m_fInAttack = 0;
			m_iNumConsumedAmmo = 0;
			m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 1.0;
			m_pPlayer->m_flNextAttack = UTIL_WeaponTimeBase() + 1.0;
				
#ifndef CLIENT_DLL
			m_pPlayer->TakeDamage( VARS(eoNullEntity), VARS(eoNullEntity), 50, DMG_POISON );
			UTIL_ScreenFade( m_pPlayer, Vector(161,188,0), 2, 0.5, 128, FFADE_IN );
#endif
			SendWeaponAnim( GAUSS_IDLE );
			
			// Player may have been killed and this weapon dropped, don't execute any more code after this!
			return;
		}
	}
}

//=========================================================
// StartFire- since all of this code has to run and then 
// call Fire(), it was easier at this point to rip it out 
// of weaponidle() and make its own function then to try to
// merge this into Fire(), which has some identical variable names 
//=========================================================
void CGauss::StartFire( void )
{
	float flDamage;
	
	UTIL_MakeVectors( m_pPlayer->pev->v_angle + m_pPlayer->pev->punchangle );
	Vector vecAiming = gpGlobals->v_forward;
	Vector vecSrc = m_pPlayer->GetGunPosition() + gpGlobals->v_forward * 16;
	
	if ( gpGlobals->time - m_pPlayer->m_flStartCharge > GetFullChargeTime() )
	{
		flDamage = 200;
	}
	else
	{
		flDamage = 200 * (( gpGlobals->time - m_pPlayer->m_flStartCharge) / GetFullChargeTime() );
	}

	if ( m_fPrimaryFire )
	{
		// fixed damage on primary attack
#ifdef CLIENT_DLL
		flDamage = 20;
#else 
		flDamage = gSkillData.plrDmgGauss;
#endif
	}

	if (m_fInAttack != 3)
	{
		//ALERT ( at_console, "Time:%f Damage:%f\n", gpGlobals->time - m_pPlayer->m_flStartCharge, flDamage );

#ifndef CLIENT_DLL
		float flZVel = m_pPlayer->pev->velocity.z;

		if ( !m_fPrimaryFire )
		{
			m_pPlayer->pev->velocity = m_pPlayer->pev->velocity - gpGlobals->v_forward * flDamage * 5;
		}

		if ( !g_pGameRules->IsMultiplayer() )

		{
			// in deathmatch, gauss can pop you up into the air. Not in single play.
			m_pPlayer->pev->velocity.z = flZVel;
		}
#endif
		// player "shoot" animation
		m_pPlayer->SetAnimation( PLAYER_ATTACK1 );
	}

	// time until aftershock 'static discharge' sound
	m_pPlayer->m_flPlayAftershock = gpGlobals->time + UTIL_SharedRandomFloat( m_pPlayer->random_seed, 0.3, 0.8 );

	Fire( vecSrc, vecAiming, flDamage );
}

void CGauss::Fire( Vector vecOrigSrc, Vector vecDir, float flDamage )
{
	m_pPlayer->m_iWeaponVolume = GAUSS_PRIMARY_FIRE_VOLUME;

	Vector vecSrc = vecOrigSrc;
	Vector vecDest = vecSrc + vecDir * 8192;
	edict_t		*pentIgnore;
	TraceResult tr, beam_tr;
	float flMaxFrac = 1.0;
	int	nTotal = 0;
	int fHasPunched = 0;
	int fFirstBeam = 1;
	int	nMaxHits = 10;

	pentIgnore = ENT( m_pPlayer->pev );

#ifdef CLIENT_DLL
	if ( m_fPrimaryFire == false )
		 g_irunninggausspred = true;
#endif
	
	// The main firing event is sent unreliably so it won't be delayed.
	PLAYBACK_EVENT_FULL( FEV_NOTHOST, m_pPlayer->edict(), m_usGaussFire, 0.0, (float *)&m_pPlayer->pev->origin, (float *)&m_pPlayer->pev->angles, flDamage, 0.0, 0, 0, m_fPrimaryFire ? 1 : 0, 0 );

	// This reliable event is used to stop the spinning sound
	// It's delayed by a fraction of second to make sure it is delayed by 1 frame on the client
	// It's sent reliably anyway, which could lead to other delays

	PLAYBACK_EVENT_FULL( FEV_NOTHOST | FEV_RELIABLE, m_pPlayer->edict(), m_usGaussFire, 0.01, (float *)&m_pPlayer->pev->origin, (float *)&m_pPlayer->pev->angles, 0.0, 0.0, 0, 0, 0, 1 );

	
	/*ALERT( at_console, "%f %f %f\n%f %f %f\n", 
		vecSrc.x, vecSrc.y, vecSrc.z, 
		vecDest.x, vecDest.y, vecDest.z );*/
	

//	ALERT( at_console, "%f %f\n", tr.flFraction, flMaxFrac );

#ifndef CLIENT_DLL
	if ( m_fPrimaryFire || m_iNumConsumedAmmo <= 2 )
	{
		CXSBeam::ShootSmall(m_pPlayer->pev, vecSrc, vecDir * 1500, flDamage);
	}
	else
	{
		int iNumBeams = 0;
		if (m_iNumConsumedAmmo <= 6)
			iNumBeams = 2;
		else if (m_iNumConsumedAmmo <= 9)
			iNumBeams = 3;
		else
			iNumBeams = 4;

		float flCycle = 0.02f;
		CXSBeam::ShootBig(m_pPlayer->pev, vecSrc, vecDir * 1200, flDamage, iNumBeams, flCycle);
	}
#endif
	// ALERT( at_console, "%d bytes\n", nTotal );
}

void CGauss::Reload( void )
{
	if ( m_pPlayer->ammo_uranium <= 0 )
		return;

	BOOL bResult = DefaultReload( GAUSS_MAX_CLIP, GAUSS_RELOAD, 81.0f / 20.0f );
	if ( bResult )
	{
		EMIT_SOUND( ENT(m_pPlayer->pev), CHAN_WEAPON, "weapons/xs_reload.wav", 0.8, ATTN_NORM );
	}
}


void CGauss::WeaponIdle( void )
{
	ResetEmptySound( );

	// play aftershock static discharge
	if ( m_pPlayer->m_flPlayAftershock && m_pPlayer->m_flPlayAftershock < gpGlobals->time )
	{
		switch (RANDOM_LONG(0,3))
		{
		case 0:	EMIT_SOUND(ENT(m_pPlayer->pev), CHAN_WEAPON, "weapons/xs_moan1.wav", RANDOM_FLOAT(0.7, 0.8), ATTN_NORM); break;
		case 1:	EMIT_SOUND(ENT(m_pPlayer->pev), CHAN_WEAPON, "weapons/xs_moan2.wav", RANDOM_FLOAT(0.7, 0.8), ATTN_NORM); break;
		case 2:	EMIT_SOUND(ENT(m_pPlayer->pev), CHAN_WEAPON, "weapons/xs_moan3.wav", RANDOM_FLOAT(0.7, 0.8), ATTN_NORM); break;
		case 3:	break; // no sound
		}
		m_pPlayer->m_flPlayAftershock = 0.0;
	}

	if (m_flTimeWeaponIdle > UTIL_WeaponTimeBase())
		return;

	if (m_fInAttack != 0)
	{
		StartFire();
		m_fInAttack = 0;
		m_iNumConsumedAmmo = 0;
		m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + FIRE2_SEQUENCE_DURATION;
	}
	else
	{
		int iAnim;
		float flRand = RANDOM_FLOAT(0, 1);
		if (flRand <= 0.5)
		{
			iAnim = GAUSS_IDLE;
			m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 63.0f / 15.0f;
		}
		else if (flRand <= 0.75)
		{
			iAnim = GAUSS_IDLE2;
			m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 63.0f / 15.0f;
		}
		else
		{
			iAnim = GAUSS_FIDGET;
			m_flTimeWeaponIdle = UTIL_WeaponTimeBase() + 93.0f / 30.0f;
		}

		SendWeaponAnim( iAnim );
		
	}
}

void CGauss::AttachToPlayer( CBasePlayer* pPlayer )
{
	CBasePlayerWeapon::AttachToPlayer( pPlayer );
	SetThink( NULL ); // Stop calling IdleThink().
}

void CGauss::IdleThink( void )
{
	pev->nextthink = gpGlobals->time + 0.1;
	StudioFrameAdvance( 0.1 );
}




class CGaussAmmo : public CBasePlayerAmmo
{
	void Spawn( void )
	{ 
		Precache( );
		SET_MODEL(ENT(pev), "models/w_xencandy.mdl");
		CBasePlayerAmmo::Spawn( );

		SetThink(&CGaussAmmo::FallThink);
		pev->nextthink = gpGlobals->time + 0.1;
	}
	void Precache( void )
	{
		PRECACHE_MODEL ("models/w_xencandy.mdl");
		PRECACHE_SOUND("items/9mmclip1.wav");
		PRECACHE_SOUND("items/weapondrop1.wav");
	}
	BOOL AddAmmo( CBaseEntity *pOther ) 
	{ 
		if (pOther->GiveAmmo( AMMO_URANIUMBOX_GIVE, "uranium", URANIUM_MAX_CARRY ) != -1)
		{
			EMIT_SOUND(ENT(pev), CHAN_ITEM, "items/9mmclip1.wav", 1, ATTN_NORM);
			return TRUE;
		}
		return FALSE;
	}
	void EXPORT FallThink(void)
	{
		pev->nextthink = gpGlobals->time + 0.1;

		if (pev->flags & FL_ONGROUND)
		{
			// clatter if we have an owner (i.e., dropped by someone)
			// don't clatter if the gun is waiting to respawn (if it's waiting, it is invisible!)
			if (!FNullEnt(pev->owner))
			{
				int pitch = 95 + RANDOM_LONG(0, 29);
				EMIT_SOUND_DYN(ENT(pev), CHAN_VOICE, "items/weapondrop1.wav", 1, ATTN_NORM, 0, pitch);
			}

			// lie flat
			pev->angles.x = 0;
			pev->angles.z = 0;

			UTIL_SetOrigin(pev, pev->origin);// link into world.
			SetThink(NULL);
		}
	}
};
LINK_ENTITY_TO_CLASS( ammo_xencandy, CGaussAmmo );

#endif
