/* Copyright (C) 2010 Wildfire Games.
 * This file is part of 0 A.D.
 *
 * 0 A.D. is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or
 * (at your option) any later version.
 *
 * 0 A.D. is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with 0 A.D.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "precompiled.h"

#include <math.h>

#include "MiniMap.h"

#include "graphics/GameView.h"
#include "graphics/MiniPatch.h"
#include "graphics/Terrain.h"
#include "graphics/TerrainTextureEntry.h"
#include "graphics/TerrainTextureManager.h"
#include "lib/ogl.h"
#include "lib/external_libraries/sdl.h"
#include "lib/bits.h"
#include "lib/timer.h"
#include "lib/sysdep/cpu.h"
#include "ps/Game.h"
#include "ps/Profile.h"
#include "ps/World.h"
#include "renderer/Renderer.h"
#include "renderer/WaterManager.h"
#include "scriptinterface/ScriptInterface.h"
#include "simulation2/Simulation2.h"
#include "simulation2/components/ICmpMinimap.h"
#include "simulation2/components/ICmpRangeManager.h"

bool g_GameRestarted = false;

static unsigned int ScaleColor(unsigned int color, float x)
{
	unsigned int r = unsigned(float(color & 0xff) * x);
	unsigned int g = unsigned(float((color>>8) & 0xff) * x);
	unsigned int b = unsigned(float((color>>16) & 0xff) * x);
	return (0xff000000 | r | g<<8 | b<<16);
}

CMiniMap::CMiniMap()
	: m_TerrainTexture(0), m_TerrainData(0), m_MapSize(0), m_Terrain(0),
	m_LOSTexture(0), m_TerrainDirty(true)
{
	AddSetting(GUIST_CColor,	"fov_wedge_color");
	AddSetting(GUIST_CStr,		"tooltip");
	AddSetting(GUIST_CStr,		"tooltip_style");
	m_Clicking = false;

	m_LOSScale = 2;
}

CMiniMap::~CMiniMap()
{
	Destroy();
}

void CMiniMap::HandleMessage(const SGUIMessage &Message)
{
	switch(Message.type)
	{
	case GUIM_MOUSE_PRESS_LEFT:
		{
			SetCameraPos();
			m_Clicking = true;
			break;
		}
	case GUIM_MOUSE_RELEASE_LEFT:
		{
			if(m_Clicking)
				SetCameraPos();
			m_Clicking = false;
			break;
		}
	case GUIM_MOUSE_DBLCLICK_LEFT:
		{
			if(m_Clicking)
				SetCameraPos();
			m_Clicking = false;
			break;
		}
	case GUIM_MOUSE_ENTER:
		{
//			g_Selection.m_mouseOverMM = true;
			break;
		}
	case GUIM_MOUSE_LEAVE:
		{
//			g_Selection.m_mouseOverMM = false;
			m_Clicking = false;
			break;
		}
	case GUIM_MOUSE_RELEASE_RIGHT:
		{
			CMiniMap::FireWorldClickEvent(SDL_BUTTON_RIGHT, 1);
			break;
		}
	case GUIM_MOUSE_DBLCLICK_RIGHT:
		{
			CMiniMap::FireWorldClickEvent(SDL_BUTTON_RIGHT, 2);
			break;
		}
	case GUIM_MOUSE_MOTION:
		{
			if (m_Clicking)
				SetCameraPos();
			break;
		}

	default:
		break;
	}	// switch
}

void CMiniMap::SetCameraPos()
{
	CTerrain* terrain = g_Game->GetWorld()->GetTerrain();

	CVector3D target;
	CPos mousePos = GetMousePos();
	target.X = CELL_SIZE * m_MapSize * ((mousePos.x - m_CachedActualSize.left) / m_CachedActualSize.GetWidth());
	target.Z = CELL_SIZE * m_MapSize * ((m_CachedActualSize.bottom - mousePos.y) / m_CachedActualSize.GetHeight());
	target.Y = terrain->GetExactGroundLevel(target.X, target.Z);
	g_Game->GetView()->MoveCameraTarget(target);
}

void CMiniMap::FireWorldClickEvent(int button, int clicks)
{
	// Determine X and Z according to proportion of mouse position and minimap
	CPos MousePos = GetMousePos();
	float x = CELL_SIZE * m_MapSize *
		((MousePos.x - m_CachedActualSize.left) / m_CachedActualSize.GetWidth());
	float z = CELL_SIZE * m_MapSize *
		((m_CachedActualSize.bottom - MousePos.y) / m_CachedActualSize.GetHeight());

	CScriptValRooted coords;
	g_ScriptingHost.GetScriptInterface().Eval("({})", coords);
	g_ScriptingHost.GetScriptInterface().SetProperty(coords.get(), "x", x, false);
	g_ScriptingHost.GetScriptInterface().SetProperty(coords.get(), "z", z, false);
	ScriptEvent("worldclick", coords);

	UNUSED2(button);
	UNUSED2(clicks);
}

// render view rect : John M. Mena
// This sets up and draws the rectangle on the mini-map
// which represents the view of the camera in the world.
void CMiniMap::DrawViewRect()
{
	// Compute the camera frustum intersected with a fixed-height plane.
	// TODO: Currently we hard-code the height, so this'll be dodgy when maps aren't the
	// expected height - how can we make it better without the view rect wobbling in
	// size while the player scrolls?
	float h = 16384.f * HEIGHT_SCALE;

	CVector3D hitPt[4];
	hitPt[0]=m_Camera->GetWorldCoordinates(0, g_Renderer.GetHeight(), h);
	hitPt[1]=m_Camera->GetWorldCoordinates(g_Renderer.GetWidth(), g_Renderer.GetHeight(), h);
	hitPt[2]=m_Camera->GetWorldCoordinates(g_Renderer.GetWidth(), 0, h);
	hitPt[3]=m_Camera->GetWorldCoordinates(0, 0, h);

	float ViewRect[4][2];
	for (int i=0;i<4;i++) {
		// convert to minimap space
		float px=hitPt[i].X;
		float pz=hitPt[i].Z;
		ViewRect[i][0]=(m_CachedActualSize.GetWidth()*px/float(CELL_SIZE*m_MapSize));
		ViewRect[i][1]=(m_CachedActualSize.GetHeight()*pz/float(CELL_SIZE*m_MapSize));
	}

	// Enable Scissoring as to restrict the rectangle
	// to only the mini-map below by retrieving the mini-maps
	// screen coords.
	const float x = m_CachedActualSize.left, y = m_CachedActualSize.bottom;
	glScissor((int)x, g_Renderer.GetHeight()-(int)y, (int)m_CachedActualSize.GetWidth(), (int)m_CachedActualSize.GetHeight());
	glEnable(GL_SCISSOR_TEST);
	glEnable(GL_LINE_SMOOTH);
	glLineWidth(2.0f);
	glColor3f(1.0f, 0.3f, 0.3f);

	// Draw the viewing rectangle with the ScEd's conversion algorithm
	glBegin(GL_LINE_LOOP);
	glVertex2f(x+ViewRect[0][0], y-ViewRect[0][1]);
	glVertex2f(x+ViewRect[1][0], y-ViewRect[1][1]);
	glVertex2f(x+ViewRect[2][0], y-ViewRect[2][1]);
	glVertex2f(x+ViewRect[3][0], y-ViewRect[3][1]);
	glEnd();

	// restore state
	glDisable(GL_SCISSOR_TEST);
	glDisable(GL_LINE_SMOOTH);
	glLineWidth(1.0f);
}

struct MinimapUnitVertex
{
	u8 r, g, b, a;
	float x, y;
};

void CMiniMap::Draw()
{
	PROFILE("minimap");

	// The terrain isn't actually initialized until the map is loaded, which
	// happens when the game is started, so abort until then.
	if(!(GetGUI() && g_Game && g_Game->IsGameStarted()))
		return;
	
	glDisable(GL_DEPTH_TEST);

	// Set our globals in case they hadn't been set before
	m_Camera      = g_Game->GetView()->GetCamera();
	m_Terrain     = g_Game->GetWorld()->GetTerrain();
	m_Width  = (u32)(m_CachedActualSize.right - m_CachedActualSize.left);
	m_Height = (u32)(m_CachedActualSize.bottom - m_CachedActualSize.top);
	m_MapSize = m_Terrain->GetVerticesPerSide();
	m_LOSMapSize = (m_MapSize + m_LOSScale - 1) / m_LOSScale; // divide and round upwards
	m_TextureSize = (GLsizei)round_up_to_pow2((size_t)m_MapSize);
	m_LOSTextureSize = (GLsizei)round_up_to_pow2((size_t)m_LOSMapSize);

	if(!m_TerrainTexture || g_GameRestarted)
		CreateTextures();


	// only update 2x / second
	// (note: since units only move a few pixels per second on the minimap,
	// we can get away with infrequent updates; this is slow)
	static double last_time;
	const double cur_time = timer_Time();
	if(cur_time - last_time > 0.5)
	{
		last_time = cur_time;

		if(m_TerrainDirty)
			RebuildTerrainTexture();

		RebuildLOSTexture();
	}

	const float texCoordMax = (float)(m_MapSize - 1) / (float)m_TextureSize;
	const float losTexCoordMax = (float)(m_LOSMapSize - 1) / (float)m_LOSTextureSize;
	const float x = m_CachedActualSize.left, y = m_CachedActualSize.bottom;
	const float x2 = m_CachedActualSize.right, y2 = m_CachedActualSize.top;
	const float z = GetBufferedZ();

	// Draw the main textured quad
	g_Renderer.BindTexture(0, m_TerrainTexture);
	glTexEnvf(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_REPLACE);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glBegin(GL_QUADS);
	glTexCoord2f(0.0f, 0.0f);
	glVertex3f(x, y, z);
	glTexCoord2f(texCoordMax, 0.0f);
	glVertex3f(x2, y, z);
	glTexCoord2f(texCoordMax, texCoordMax);
	glVertex3f(x2, y2, z);
	glTexCoord2f(0.0f, texCoordMax);
	glVertex3f(x, y2, z);
	glEnd();

	/* // TODO: reimplement with new sim system
	// Shade territories by player
	CTerritoryManager* territoryMgr = g_Game->GetWorld()->GetTerritoryManager();
	std::vector<CTerritory*>& territories = territoryMgr->GetTerritories();

	PROFILE_START("minimap territory shade");
	
	glDisable(GL_TEXTURE_2D);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	for( size_t i=0; i<territories.size(); i++ )
	{
		if( territories[i]->owner->GetPlayerID() == 0 )
			continue;
		std::vector<CVector2D>& boundary = territories[i]->boundary;
		SPlayerColour col = territories[i]->owner->GetColour();
		glColor4f(col.r, col.g, col.b, 0.25f);
		glBegin(GL_POLYGON);
		for( size_t j=0; j<boundary.size(); j++ )
		{
			float fx = boundary[j].x / (m_Terrain->GetTilesPerSide() * CELL_SIZE);
			float fy = boundary[j].y / (m_Terrain->GetTilesPerSide() * CELL_SIZE);
			glVertex3f( x*(1-fx) + x2*fx, y*(1-fy) + y2*fy, z );
		}
		glEnd();
	}
	glDisable(GL_BLEND);

	PROFILE_END("minimap territory shade");

	// Draw territory boundaries
	glEnable(GL_LINE_SMOOTH);
	glLineWidth(1.0f);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glColor4f(0.8f, 0.8f, 0.8f, 0.8f);
	for( size_t i=0; i<territories.size(); i++ )
	{
		std::vector<CVector2D>& boundary = territories[i]->boundary;
		glBegin(GL_LINE_LOOP);
		for( size_t j=0; j<boundary.size(); j++ )
		{
			float fx = boundary[j].x / (m_Terrain->GetTilesPerSide() * CELL_SIZE);
			float fy = boundary[j].y / (m_Terrain->GetTilesPerSide() * CELL_SIZE);
			glVertex3f( x*(1-fx) + x2*fx, y*(1-fy) + y2*fy, z );
		}
		glEnd();
	}
	glLineWidth(1.0f);
	glDisable(GL_LINE_SMOOTH);
	glDisable(GL_BLEND);
	*/

	// Draw the LOS quad in black, using alpha values from the LOS texture
	g_Renderer.BindTexture(0, m_LOSTexture);
	glTexEnvi(GL_TEXTURE_ENV, GL_TEXTURE_ENV_MODE, GL_COMBINE);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_RGB_ARB, GL_REPLACE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_RGB_ARB, GL_PRIMARY_COLOR_ARB);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_RGB_ARB, GL_SRC_COLOR);
	glTexEnvi(GL_TEXTURE_ENV, GL_COMBINE_ALPHA_ARB, GL_REPLACE);
	glTexEnvi(GL_TEXTURE_ENV, GL_SOURCE0_ALPHA_ARB, GL_TEXTURE);
	glTexEnvi(GL_TEXTURE_ENV, GL_OPERAND0_ALPHA_ARB, GL_SRC_ALPHA);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
	glEnable(GL_BLEND);
	glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
	glBegin(GL_QUADS);
	glColor3f(0.0f, 0.0f, 0.0f);
	glTexCoord2f(0.0f, 0.0f);
	glVertex3f(x, y, z);
	glTexCoord2f(losTexCoordMax, 0.0f);
	glVertex3f(x2, y, z);
	glTexCoord2f(losTexCoordMax, losTexCoordMax);
	glVertex3f(x2, y2, z);
	glTexCoord2f(0.0f, losTexCoordMax);
	glVertex3f(x, y2, z);
	glEnd();
	glDisable(GL_BLEND);

	PROFILE_START("minimap units");

	// Don't enable GL_POINT_SMOOTH because it's far too slow
	// (~70msec/frame on a GF4 rendering a thousand points)
	glPointSize(3.f);

	float sx = (float)m_Width / ((m_MapSize - 1) * CELL_SIZE);
	float sy = (float)m_Height / ((m_MapSize - 1) * CELL_SIZE);

	CSimulation2* sim = g_Game->GetSimulation2();
	const CSimulation2::InterfaceList& ents = sim->GetEntitiesWithInterface(IID_Minimap);

	std::vector<MinimapUnitVertex> vertexArray;
	vertexArray.reserve(ents.size());

	CmpPtr<ICmpRangeManager> cmpRangeManager(*sim, SYSTEM_ENTITY);
	debug_assert(!cmpRangeManager.null());

	for (CSimulation2::InterfaceList::const_iterator it = ents.begin(); it != ents.end(); ++it)
	{
		MinimapUnitVertex v;
		ICmpMinimap* cmpMinimap = static_cast<ICmpMinimap*>(it->second);
		entity_pos_t posX, posZ;
		if (cmpMinimap->GetRenderData(v.r, v.g, v.b, posX, posZ))
		{
			ICmpRangeManager::ELosVisibility vis = cmpRangeManager->GetLosVisibility(it->first, g_Game->GetPlayerID());
			if (vis != ICmpRangeManager::VIS_HIDDEN)
			{
				v.a = 255;
				v.x = x + posX.ToFloat()*sx;
				v.y = y - posZ.ToFloat()*sy;
				vertexArray.push_back(v);
			}
		}
	}

	if (!vertexArray.empty())
	{
		glPushMatrix();
		glTranslatef(0, 0, z);

		glInterleavedArrays(GL_C4UB_V2F, sizeof(MinimapUnitVertex), &vertexArray[0]);
		glDrawArrays(GL_POINTS, 0, (GLsizei)vertexArray.size());

		glDisableClientState(GL_COLOR_ARRAY);
		glDisableClientState(GL_VERTEX_ARRAY);

		glPopMatrix();
	}

	PROFILE_END("minimap units");

	DrawViewRect();

	// Reset everything back to normal
	glPointSize(1.0f);
	glEnable(GL_TEXTURE_2D);
	glEnable(GL_DEPTH_TEST);
}

void CMiniMap::CreateTextures()
{
	Destroy();

	// Create terrain texture
	glGenTextures(1, &m_TerrainTexture);
	g_Renderer.BindTexture(0, m_TerrainTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_TextureSize, m_TextureSize, 0, GL_BGRA_EXT, GL_UNSIGNED_BYTE, 0);
	m_TerrainData = new u32[(m_MapSize - 1) * (m_MapSize - 1)];
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);

	// Create LOS texture
	glGenTextures(1, &m_LOSTexture);
	g_Renderer.BindTexture(0, m_LOSTexture);
	glTexImage2D(GL_TEXTURE_2D, 0, GL_ALPHA8, m_LOSTextureSize, m_LOSTextureSize, 0, GL_ALPHA, GL_UNSIGNED_BYTE, 0);
	m_LOSData.resize(m_LOSMapSize * m_LOSMapSize);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_CLAMP);
	glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP);

	// Rebuild and upload both of them
	RebuildTerrainTexture();
	RebuildLOSTexture();
}


void CMiniMap::RebuildTerrainTexture()
{
	u32 x = 0;
	u32 y = 0;
	u32 w = m_MapSize - 1;
	u32 h = m_MapSize - 1;
	float waterHeight = g_Renderer.GetWaterManager()->m_WaterHeight;

	m_TerrainDirty = false;

	for(u32 j = 0; j < h; j++)
	{
		u32 *dataPtr = m_TerrainData + ((y + j) * (m_MapSize - 1)) + x;
		for(u32 i = 0; i < w; i++)
		{
			float avgHeight = ( m_Terrain->GetVertexGroundLevel((int)i, (int)j)
					+ m_Terrain->GetVertexGroundLevel((int)i+1, (int)j)
					+ m_Terrain->GetVertexGroundLevel((int)i, (int)j+1)
					+ m_Terrain->GetVertexGroundLevel((int)i+1, (int)j+1)
				) / 4.0f;

			if(avgHeight < waterHeight)
			{
				*dataPtr++ = 0xff304080;		// TODO: perhaps use the renderer's water color?
			}
			else
			{
				int hmap = ((int)m_Terrain->GetHeightMap()[(y + j) * m_MapSize + x + i]) >> 8;
				int val = (hmap / 3) + 170;

				u32 color = 0xFFFFFFFF;

				CMiniPatch *mp = m_Terrain->GetTile(x + i, y + j);
				if(mp)
				{
					CTerrainTextureEntry *tex = mp->GetTextureEntry();
					if(tex)
					{
						// If the texture can't be loaded yet, set the dirty flags
						// so we'll try regenerating the terrain texture again soon
						if(!tex->GetTexture()->TryLoad())
							m_TerrainDirty = true;

						color = tex->GetBaseColor();
					}
				}

				*dataPtr++ = ScaleColor(color, float(val) / 255.0f);
			}
		}
	}

	// Upload the texture
	g_Renderer.BindTexture(0, m_TerrainTexture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_MapSize - 1, m_MapSize - 1, GL_BGRA_EXT, GL_UNSIGNED_BYTE, m_TerrainData);
}


void CMiniMap::RebuildLOSTexture()
{
	PROFILE("rebuild minimap: los");

	ssize_t w = m_MapSize;
	ssize_t h = m_MapSize;

	CmpPtr<ICmpRangeManager> cmpRangeManager(*g_Game->GetSimulation2(), SYSTEM_ENTITY);
	if (cmpRangeManager.null() || cmpRangeManager->GetLosRevealAll(g_Game->GetPlayerID()))
	{
		memset(&m_LOSData[0], 0, m_LOSData.size());
	}
	else
	{
		ICmpRangeManager::CLosQuerier los (cmpRangeManager->GetLosQuerier(g_Game->GetPlayerID()));

		u8 *dataPtr = &m_LOSData[0];

		for (ssize_t j = 0; j < h; j += m_LOSScale)
		{
			for (ssize_t i = 0; i < w; i += m_LOSScale)
			{
				if (los.IsVisible(i, j))
					*dataPtr++ = 0;
				else if (los.IsExplored(i, j))
					*dataPtr++ = 76;
				else
					*dataPtr++ = 255;
			}
		}
	}

	// Upload the texture
	g_Renderer.BindTexture(0, m_LOSTexture);
	glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, m_LOSMapSize, m_LOSMapSize, GL_ALPHA, GL_UNSIGNED_BYTE, &m_LOSData[0]);
}

void CMiniMap::Destroy()
{
	if(m_TerrainTexture)
	{
		glDeleteTextures(1, &m_TerrainTexture);
		m_TerrainTexture = 0;
	}

	if(m_LOSTexture)
	{
		glDeleteTextures(1, &m_LOSTexture);
		m_LOSTexture = 0;
	}

	delete[] m_TerrainData; m_TerrainData = 0;
}
