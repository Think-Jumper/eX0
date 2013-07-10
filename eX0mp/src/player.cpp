#include "globals.h"

int		nPlayerCount = 0;
int		iLocalPlayerID = 0;
string	sLocalPlayerName = "New Player";
CPlayer	*oPlayers[32];

//float	fPlayerTickTime;// = 0.025f;
//float	fPlayerTickTime = 0.050f;
//float	fPlayerTickTime = 0.100f;
//float	fPlayerTickTime = 1.0f;

// implementation of the player class
CPlayer::CPlayer()
{
	// init vars
	fX = 0;
	fY = 0;
	fOldX = 0;
	fOldY = 0;
	fVelX = 0;
	fVelY = 0;
	fZ = 0;
	fOldZ = 0;
	iIsStealth = 0;
	nMoveDirection = -1;
	iSelWeapon = 2;
	fAimingDistance = 200.0;
	fHealth = 100;
	sName = "Unnamed Player";
	iTeam = 0;
	bEmptyClicked = true;
	fTicks = 0;
	//fUpdateTicks = 0;
	fTickTime = 0;
	bFirstUpdate = true;

	// Network related
	bConnected = false;
}

CPlayer::~CPlayer()
{
	// nothing to do here yet
}

void CPlayer::Tick()
{
glfwLockMutex(oPlayerTick);

	// is the player dead?
	if (IsDead() || !bConnected) {
		glfwUnlockMutex(oPlayerTick);
		return;
	}

	oWeapons[iSelWeapon].Tick();

	//fTicks += (double)dTimePassed;
	fTicks = dCurTime - (dNextTickTime - 1.0 / cCommandRate);
	//fUpdateTicks += dTimePassed;
	//while (fTicks >= fTickTime)
	while (dCurTime >= dNextTickTime)
	{
		//fTicks -= fTickTime;
		dNextTickTime += 1.0 / cCommandRate;
		fTicks = dCurTime - (dNextTickTime - 1.0 / cCommandRate);

		if (iID == iLocalPlayerID) {
			if (oUnconfirmedMoves.size() < 100)
			{
				// calculate player trajectory
				CalcTrajs();

				// do collision response for player
				CalcColResp();

				++cCurrentCommandSequenceNumber;
				Input_t oInput;
				oInput.cMoveDirection = (char)nMoveDirection;
				oInput.fZ = GetZ();
				//if (oUnconfirmedInputs.size() < 101)
				//oUnconfirmedInputs.push_back(oInput);
				Move_t oMove;
				oMove.oInput = oInput;
				oMove.oState.fX = GetX(); oMove.oState.fY = GetY(); oMove.oState.fZ = GetZ();
				oUnconfirmedMoves.push(oMove, cCurrentCommandSequenceNumber);

				//iTempInt = __max(iTempInt, oUnconfirmedMoves.size() - 1);
				iTempInt = max<int>(iTempInt, (int)oUnconfirmedMoves.size() - 1);

				// Send the Update Own Position packet
				CPacket oUpdateOwnPositionPacket;
				oUpdateOwnPositionPacket.pack("ccc", (u_char)1, // packet type
													 cCurrentCommandSequenceNumber, // sequence number
													 (u_char)oUnconfirmedMoves.size());
				for (u_char it1 = oUnconfirmedMoves.begin(); it1 != oUnconfirmedMoves.end(); ++it1)
				{
					oUpdateOwnPositionPacket.pack("cf", oUnconfirmedMoves[it1].oInput.cMoveDirection,
														oUnconfirmedMoves[it1].oInput.fZ);
				}
				if ((rand() % 100) >= 0 || iLocalPlayerID != 0) // DEBUG: Simulate packet loss
					oUpdateOwnPositionPacket.SendUdp();

				// Ping time calculation
				if (nPingPacketNumber < -1) {
					++nPingPacketNumber;
				} else if (nPingPacketNumber == -1) {
					dPingPacketTime = glfwGetTime();
					nPingPacketNumber = cCurrentCommandSequenceNumber;
				}

				// DEBUG: Keep state history for local player
				/*if ((rand() % 100) >= 0) {
					SequencedState_t oSequencedState;
					oSequencedState.cSequenceNumber = cCurrentCommandSequenceNumber;
					oSequencedState.oState = oMove.oState;
					PushStateHistory(oSequencedState);
				}*/
			}
		} else {
			// calculate player trajectory
			//CalcTrajs();
			/*fOldX = fX;
			fOldY = fY;
			fX += fVelX;
			fY += fVelY;

			// do collision response for player
			CalcColResp();*/
			//++cCurrentCommandSequenceNumber;
		}
	}

	UpdateInterpolatedPos();

glfwUnlockMutex(oPlayerTick);
}

// returns number of clips left in the selected weapon
int CPlayer::GetSelClips()
{
	return oWeapons[iSelWeapon].GetClips();
}
int CPlayer::GetSelClipAmmo()
{
	return oWeapons[iSelWeapon].GetClipAmmo();
}

// fire selected weapon
void CPlayer::Fire()
{
	// is the player dead?
	if (IsDead()) return;

	// fire the selected weapon
	oWeapons[iSelWeapon].Fire();
}

// reload selected weapon
void CPlayer::Reload()
{
	// is the player dead?
	if (IsDead()) return;

	oWeapons[iSelWeapon].Reload();
}

bool CPlayer::IsReloading()
{
	return oWeapons[iSelWeapon].IsReloading();
}

// buy a clip for selected weapon
void CPlayer::BuyClip()
{
	// is the player dead?
	if (IsDead()) return;

	// TODO: some kind of money system?
	// just give it to whoever
	oWeapons[iSelWeapon].GiveClip();
}

void CPlayer::SetTeam(int iValue)
{
	iTeam = iValue;
}

void CPlayer::SetStealth(bool bOn)
{
	// is the player dead?
	if (IsDead()) return;

	iIsStealth = (int)bOn;

	if (iIsStealth) printf("Stealth ON!\n");
}

bool CPlayer::IsDead()
{
	return (fHealth <= 0);
}

float CPlayer::GetHealth()
{
	return fHealth;
}

void CPlayer::GiveHealth(float fValue)
{
	// is the player dead?
	if (IsDead()) return;

	fHealth += fValue;
	if (fHealth < 0) fHealth = 0;
}

void CPlayer::CalcTrajs()
{
	// is the player dead?
	if (IsDead()) return;

#if 0
	// Update the player velocity (acceleration)
	if (nMoveDirection == -1)
	{
		fVelX = 0.0;
		fVelY = 0.0;
	}
	else
	{
		fVelX = (fTickTime / 0.050f) * Math::Sin((float)nMoveDirection * 0.785398f + fZ) * (3.5f - iIsStealth * 2.5f);
		fVelY = (fTickTime / 0.050f) * Math::Cos((float)nMoveDirection * 0.785398f + fZ) * (3.5f - iIsStealth * 2.5f);
	}
#else
	// DEBUG - this is STILL not finished, need to redo properly
	// need to do linear acceleration and deceleration
	if (nMoveDirection == -1)
	{
		Vector2 oVel(fVelX, fVelY);
		float fLength = oVel.Unitize();
		fLength -= 0.25f;
		if (fLength > 0) oVel *= fLength; else oVel *= 0;
		fVelX = oVel.x;
		fVelY = oVel.y;
	}
	else
	{
		fVelX += 0.25f * Math::Sin((float)nMoveDirection * 0.785398f + fZ);
		fVelY += 0.25f * Math::Cos((float)nMoveDirection * 0.785398f + fZ);
		Vector2 oVel(fVelX, fVelY);
		float fLength = oVel.Unitize();
		if (fLength - 0.5f > 3.5f - iIsStealth * 1.5f) {
			fLength -= 0.5f;
			oVel *= fLength;
			fVelX = oVel.x;
			fVelY = oVel.y;
		} else if (fLength > 3.5f - iIsStealth * 1.5f) {
			oVel *= 3.5f - iIsStealth * 1.5f;
			fVelX = oVel.x;
			fVelY = oVel.y;
		}
	}
#endif

	// Update the player positions
	fOldX = fX;
	fOldY = fY;
	fX += fVelX;
	fY += fVelY;
}

void CPlayer::CalcColResp()
{
	// is the player dead?
	if (IsDead()) return;

	int			iWhichCont, iWhichVert, iCounter = 0;
	Vector2		oVector, oClosestPoint;
	Real		oShortestDistance;
	//Real		oDistance;
	//float		fVelXPercentage, fVelYPercentage;

	while (true)
	{
		// check for collision
		if (!ColHandCheckPlayerPos(&fX, &fY, &oShortestDistance, &oClosestPoint, &iWhichCont, &iWhichVert))
		{
			// DEBUG - player-player collision
			/*for (int iLoop1 = 0; iLoop1 < nPlayerCount; iLoop1++)
			{
				// dont check for collision with yourself
				if (iLoop1 == iID)
					continue;

				// calculate the displacement
				oVector.x = oPlayers[iID]->GetX() - oPlayers[iLoop1]->GetX();
				oVector.y = oPlayers[iID]->GetY() - oPlayers[iLoop1]->GetY();
				oDistance = oVector.Length();

				if (oDistance < oShortestDistance)
				{
					oShortestDistance = oDistance;
					oClosestPoint.x = oPlayers[iLoop1]->GetX();
					oClosestPoint.y = oPlayers[iLoop1]->GetY();
				}
			}*/

			oVector.x = oClosestPoint.x - fX;
			oVector.y = oClosestPoint.y - fY;
			//fX = fX - (oVector.x / (oShortestDistance / PLAYER_HALF_WIDTH) - oVector.x);
			//fY = fY - (oVector.y / (oShortestDistance / PLAYER_HALF_WIDTH) - oVector.y);
			fX -= (float)(oVector.x * PLAYER_HALF_WIDTH / oShortestDistance - oVector.x);
			fY -= (float)(oVector.y * PLAYER_HALF_WIDTH / oShortestDistance - oVector.y);
		}
		else
			break;
	}

	// player-player collision - only check if we're moving
	/*if (fVelX || fVelY)
	{
		for (int iLoop1 = 0; iLoop1 < nPlayerCount; iLoop1++)
		{
			// dont check for collision with yourself
			if (iLoop1 == iID)
				continue;

			// calculate the displacement
			oVector.x = oPlayers[iID]->GetX() - oPlayers[iLoop1]->GetX();
			oVector.y = oPlayers[iID]->GetY() - oPlayers[iLoop1]->GetY();
			oShortestDistance = oVector.Length();

			if (oPlayers[iLoop1]->GetVelX() || oPlayers[iLoop1]->GetVelY())
			// the other player is moving
			{
				if (oShortestDistance < PLAYER_WIDTH - PLAYER_COL_DET_TOLERANCE)
				// we're colliding with the other player
				{
					fVelXPercentage = Math::FAbs(fVelX / (Math::FAbs(fVelX) + Math::FAbs(oPlayers[iLoop1]->GetVelX())));
					fVelYPercentage = Math::FAbs(fVelY / (Math::FAbs(fVelY) + Math::FAbs(oPlayers[iLoop1]->GetVelY())));

					// move us back slightly
					oPlayers[iID]->SetX(oPlayers[iID]->GetX() + (oVector.x * PLAYER_WIDTH / oShortestDistance - oVector.x) * fVelXPercentage);
					oPlayers[iID]->SetY(oPlayers[iID]->GetY() + (oVector.y * PLAYER_WIDTH / oShortestDistance - oVector.y) * fVelYPercentage);

					// move the other player away slightly
					oPlayers[iLoop1]->SetX(oPlayers[iLoop1]->GetX() - (oVector.x * PLAYER_WIDTH / oShortestDistance - oVector.x) * (1.0 - fVelXPercentage));
					oPlayers[iLoop1]->SetY(oPlayers[iLoop1]->GetY() - (oVector.y * PLAYER_WIDTH / oShortestDistance - oVector.y) * (1.0 - fVelYPercentage));

					CalcColResp();
				}
			}
			else
			// the other player is NOT moving
			{
				if (oShortestDistance < PLAYER_WIDTH - PLAYER_COL_DET_TOLERANCE)
				// we're colliding with the other player
				{
					// move us back
					fX += oVector.x * PLAYER_WIDTH / oShortestDistance - oVector.x;
					fY += oVector.y * PLAYER_WIDTH / oShortestDistance - oVector.y;

					// check for collision
					if (!ColHandCheckPlayerPos(&fX, &fY))
					{
						//fX = fOldX;
						//fY = fOldY;
						CalcColResp();
					}
				}
			}
		}
	}*/
}

float CPlayer::GetIntX()
{
	return fIntX;
}

float CPlayer::GetIntY()
{
	return fIntY;
}

float CPlayer::GetX()
{
	return fX;
}

float CPlayer::GetY()
{
	return fY;
}

float CPlayer::GetOldX()
{
	return fOldX;
}

float CPlayer::GetOldY()
{
	return fOldY;
}

void CPlayer::SetX(float fValue)
{
	// is the player dead?
	if (IsDead()) return;

	fX = fValue;
}

void CPlayer::SetY(float fValue)
{
	// is the player dead?
	if (IsDead()) return;

	fY = fValue;
}

void CPlayer::SetOldX(float fValue)
{
	// is the player dead?
	if (IsDead()) return;

	fOldX = fValue;
}

void CPlayer::SetOldY(float fValue)
{
	// is the player dead?
	if (IsDead()) return;

	fOldY = fValue;
}

void CPlayer::Position(float fNewX, float fNewY, float fNewZ)
{
	// is the player dead?
	if (IsDead()) return;

	fIntX = fOldX = fX = fNewX;
	fIntY = fOldY = fY = fNewY;
	fZ = fOldZ = fNewZ;

	fVelX = 0.0f;
	fVelY = 0.0f;

	// Reset state history
	oStateHistory.clear();
	oOnlyKnownState.fX = fNewX;
	oOnlyKnownState.fY = fNewY;
	oOnlyKnownState.fZ = fNewZ;
}

void CPlayer::Position(float fNewX, float fNewY, float fNewZ, u_char cSequenceNumber)
{
		// is the player dead?
	if (IsDead()) return;

	fIntX = fOldX = fX = fNewX;
	fIntY = fOldY = fY = fNewY;
	fZ = fOldZ = fNewZ;

	fVelX = 0.0f;
	fVelY = 0.0f;

	// Reset state history
	oStateHistory.clear();
	SequencedState_t oSequencedState;
	oSequencedState.cSequenceNumber = cSequenceNumber;
	oSequencedState.oState.fX = fNewX;
	oSequencedState.oState.fY = fNewY;
	oSequencedState.oState.fZ = fNewZ;
	oStateHistory.push_front(oSequencedState);
}

float CPlayer::GetVelX()
{
	return fVelX;
}

float CPlayer::GetVelY()
{
	return fVelY;
}

void CPlayer::SetVelX(float fValue)
{
	// is the player dead?
	if (IsDead()) return;

	fVelX = fValue;
}

void CPlayer::SetVelY(float fValue)
{
	// is the player dead?
	if (IsDead()) return;

	fVelY = fValue;
}

float CPlayer::GetZ()
{
	//if (iID == iLocalPlayerID)
	//{
		// DEBUG - yet another hack.. replace it with some proper network-syncronyzed view bobbing
		//return fZ + Math::Sin(glfwGetTime() * 7.5) * GetVelocity() * 0.005;
		return fZ;
	/*} else {
		if (fUpdateTicks / (1.0f / cUpdateRate) <= 1.0) {
			float fDiffZ = fZ - fOldZ;
			if (fDiffZ >= Math::PI) fDiffZ -= Math::TWO_PI;
			if (fDiffZ < -Math::PI) fDiffZ += Math::TWO_PI;
			return fOldZ + fDiffZ * fUpdateTicks / (1.0f / cUpdateRate);
		} else
			return fZ;
	}*/
}

float CPlayer::GetVelocity()
{
	// is the player dead?
	if (fHealth <= 0.0f) return 0.0f;

	//return (fVelX || fVelY) ? 3.5f - iIsStealth * 2.0f : 0.0f;
	return Math::Sqrt(fVelX * fVelX + fVelY * fVelY);
}

void CPlayer::MoveDirection(int nDirection)
{
	nMoveDirection = nDirection;
}

void CPlayer::Rotate(float fAmount)
{
	// is the player dead?
	if (IsDead()) return;

	SetZ(fZ + fAmount);
}

void CPlayer::SetZ(float fValue)
{
	// is the player dead?
	if (IsDead()) return;

	fZ = fValue;
	if (fZ >= Math::TWO_PI) fZ -= Math::TWO_PI;
	if (fZ < 0.0) fZ += Math::TWO_PI;
}

void CPlayer::Render()
{
	// select player color
	/*if (iID != iLocalPlayerID && !Trace(oPlayers[iLocalPlayerID]->GetIntX(), oPlayers[iLocalPlayerID]->GetIntY(), fIntX, fIntY))
		glColor3f(0.2, 0.5, 0.2);
	else */if (IsDead())
		glColor3f(0.1f, 0.1f, 0.1f);
	else if (iTeam == 0)
		glColor3f(1, 0, 0);
	else if (iTeam == 1)
		glColor3f(0, 0, 1);
	else
		glColor3f(0, 1, 0);

	if (bWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	if (iID == iLocalPlayerID)
	// local player
	{
		OglUtilsSetMaskingMode(NO_MASKING_MODE);
		glLoadIdentity();
		RenderOffsetCamera(true);
		/*glBegin(GL_LINES);
			glVertex2i(0, 11);
			glVertex2i(0, 3);
		glEnd();*/
		glBegin(GL_QUADS);
			glVertex2i(-1, 11);
			glVertex2i(-1, 3);
			glVertex2i(1, 3);
			glVertex2i(1, 11);
		glEnd();
		gluPartialDisk(oQuadricObj, 6, 8, 12, 1, 30.0, 300.0);

		// Draw the aiming-guide
		OglUtilsSetMaskingMode(WITH_MASKING_MODE);
		glLineWidth(1.0);
		glEnable(GL_LINE_SMOOTH);
		glEnable(GL_BLEND);
		glShadeModel(GL_SMOOTH);
		glBegin(GL_LINES);
			glColor4f(0.9f, 0.2f, 0.1f, 0.5f);
			glVertex2i(0, 11);
			glColor4f(0.9f, 0.2f, 0.1f, 0.0f);
			glVertex2i(0, 600);
		glEnd();
		glShadeModel(GL_FLAT);
		glDisable(GL_BLEND);
		glDisable(GL_LINE_SMOOTH);
		glLineWidth(2.0);

		// Draw the cross section of the aiming-guide
		/*glBegin(GL_LINES);
			glColor3f(0.9, 0.2, 0.1);
			glVertex2i(-5, fAimingDistance);
			glVertex2i(5, fAimingDistance);
		glEnd();*/

		RenderOffsetCamera(false);
	}
	else
	// not local player
	{
		//if (Trace(oPlayers[iLocalPlayerID]->GetIntX(), oPlayers[iLocalPlayerID]->GetIntY(), fIntX, fIntY))
		//{
			glPushMatrix();
			RenderOffsetCamera(false);
			glTranslatef(fIntX, fIntY, 0);
			glRotatef(this->GetZ() * Math::RAD_TO_DEG, 0, 0, -1);
			glBegin(GL_QUADS);
				glVertex2i(-1, 11);
				glVertex2i(-1, 3);
				glVertex2i(1, 3);
				glVertex2i(1, 11);
			glEnd();
			gluPartialDisk(oQuadricObj, 6, 8, 12, 1, 30.0, 300.0);

		// Draw the aiming-guide
		/*glLineWidth(1.0);
		glEnable(GL_LINE_SMOOTH);
		glEnable(GL_BLEND);
		glShadeModel(GL_SMOOTH);
		glRotatef(this->GetZ() * Math::RAD_TO_DEG, 0, 0, -1);
		glBegin(GL_LINES);
			glColor4f(0.1, 0.2, 0.9, 0.5);
			glVertex2i(0, 11);
			glColor4f(0.1, 0.2, 0.9, 0.0);
			glVertex2i(0, 400);
		glEnd();
		glRotatef(this->GetZ() * Math::RAD_TO_DEG, 0, 0, 1);
		glShadeModel(GL_FLAT);
		glDisable(GL_BLEND);
		glDisable(GL_LINE_SMOOTH);
		glLineWidth(2.0);*/

			glPopMatrix();
		//}
	}
	if (bWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

	/*glBegin(GL_POINTS);
		glColor3f(0, 1, 0);
		glVertex2i(GetX(), GetY());
		glVertex2i(GetOldX(), GetOldY());
	glEnd();*/

	/*Ray2	oRay;
	oRay.Origin().x = fIntX;
	oRay.Origin().y = fIntY;
	oRay.Direction().x = -Math::Sin(fZ) * 100.0;
	oRay.Direction().y = -Math::Cos(fZ) * 100.0;
	Vector2	oVector = ColHandTrace(oRay);
	glBegin(GL_LINES);
		//glVertex2i((oRay.Origin() + oRay.Direction()).x, (oRay.Origin() + oRay.Direction()).y);
		glVertex2i(oVector.x, oVector.y);
		glVertex2i(oRay.Origin().x, oRay.Origin().y);
	glEnd();*/

	// DEBUG some debug info
	if (1 && !glfwGetKey(GLFW_KEY_TAB) && iID == iLocalPlayerID)
	{
		OglUtilsSwitchMatrix(SCREEN_SPACE_MATRIX);
		OglUtilsSetMaskingMode(NO_MASKING_MODE);
		glColor3f(1, 1, 1);

		sTempString = "x: " + ftos(fIntX);
		glLoadIdentity();
		OglUtilsPrint(0, 20, 0, false, (char *)sTempString.c_str());
		sTempString = "y: " + ftos(fIntY);
		glLoadIdentity();
		OglUtilsPrint(0, 30, 0, false, (char *)sTempString.c_str());
		sTempString = "z: " + ftos(fZ);
		glLoadIdentity();
		OglUtilsPrint(0, 40, 0, false, (char *)sTempString.c_str());
		sTempString = "velocity: " + ftos(GetVelocity());
		glLoadIdentity();
		OglUtilsPrint(150, 20, 0, false, (char *)sTempString.c_str());

		for (int iLoop1 = 0; iLoop1 < nPlayerCount; ++iLoop1)
		{
			if (PlayerGet(iLoop1)->bConnected) {
				glLoadIdentity();
				sTempString = (string)"pl#" + itos(iLoop1) + " name: '" + oPlayers[iLoop1]->GetName()
					+ "' hp: " + itos((int)oPlayers[iLoop1]->GetHealth())
					//+ " ccsn: " + itos(oPlayers[iLoop1]->cCurrentCommandSequenceNumber)
					+ " lacsn: " + itos(oPlayers[iLoop1]->cLastAckedCommandSequenceNumber)
					+ " vel: " + ftos(oPlayers[iLoop1]->GetVelocity());
				OglUtilsPrint(0, 50 + iLoop1 * 10, 0, false, (char *)sTempString.c_str());
			}
		}

		sTempString = "max oLocallyPredictedInputs.size(): " + itos(iTempInt);
		glLoadIdentity();
		OglUtilsPrint(0, 50 + nPlayerCount * 10, 0, false, (char *)sTempString.c_str());
		//sTempString = "fTempFloat: " + ftos(fTempFloat);
		sTempString = "Latency: " + ftos(fLastLatency);
		glLoadIdentity();
		OglUtilsPrint(0, 65 + nPlayerCount * 10, 0, false, (char *)sTempString.c_str());

		// Networking info
		sTempString = "cCurrentCommandSequenceNumber = " + itos(cCurrentCommandSequenceNumber);
		glLoadIdentity();
		OglUtilsPrint(0, 90 + nPlayerCount * 10, 0, false, (char *)sTempString.c_str());
		//sTempString = "cLastAckedCommandSequenceNumber = " + itos(cLastAckedCommandSequenceNumber);
		//glLoadIdentity();
		//OglUtilsPrint(0, 105 + nPlayerCount * 10, 0, false, (char *)sTempString.c_str());
		sTempString = "cLastUpdateSequenceNumber = " + itos(cLastUpdateSequenceNumber);
		glLoadIdentity();
		OglUtilsPrint(0, 120 + nPlayerCount * 10, 0, false, (char *)sTempString.c_str());
		sTempString = "oUnconfirmedMoves.size() = " + itos(oUnconfirmedMoves.size());
		glLoadIdentity();
		OglUtilsPrint(0, 135 + nPlayerCount * 10, 0, false, (char *)sTempString.c_str());

		OglUtilsSwitchMatrix(WORLD_SPACE_MATRIX);
		RenderOffsetCamera(false);
		OglUtilsSetMaskingMode(WITH_MASKING_MODE);
	}

if (iID == iLocalPlayerID) ++counter1;
}

void CPlayer::PushStateHistory(SequencedState_t &oSequencedState)
{
	// Insert the only known state if history is empty
	if (oStateHistory.size() == 0) {
		SequencedState_t oFirstSequencedState;
		oFirstSequencedState.cSequenceNumber = (u_char)(oSequencedState.cSequenceNumber - 1);
		oFirstSequencedState.oState = oOnlyKnownState;
		oStateHistory.push_front(oFirstSequencedState);
	}

	if (oStateHistory.empty() ||
		oSequencedState.cSequenceNumber != oStateHistory.front().cSequenceNumber)
	{
		if (oStateHistory.size() >= 1000) oStateHistory.pop_back();
		oStateHistory.push_front(oSequencedState);
	}
}

State_t CPlayer::GetStateInPast(float fTimeAgo)
{
	float fHistoryX;
	float fHistoryY;
	float fHistoryZ;

	/*if (fTimeAgo == 0.0f) {
		fHistoryX = GetIntX();
		fHistoryY = GetIntY();
		fHistoryZ = GetZ();
	} else if (oStateHistory.size() == 1) {
		fHistoryX = oStateHistory.front().fX;
		fHistoryY = oStateHistory.front().fY;
		fHistoryZ = oStateHistory.front().fZ;
	} else {
		u_char cHistoryPoint = cCurrentCommandSequenceNumber;
		while (fTimeAgo > fTicks) {
			if (cHistoryPoint == oStateHistory.begin()) {
				break;
			} else {
				--cHistoryPoint;
				fTimeAgo -= fTickTime;
			}
		}
		if (cHistoryPoint != oStateHistory.begin()) {
			State_t oToState = oStateHistory[cHistoryPoint];
			--cHistoryPoint;
			State_t oFromState = oStateHistory[cHistoryPoint];

			fHistoryX = oFromState.fX + (oToState.fX - oFromState.fX) * (fTicks - fTimeAgo) / fTickTime;
			fHistoryY = oFromState.fY + (oToState.fY - oFromState.fY) * (fTicks - fTimeAgo) / fTickTime;

			float fDiffZ = oToState.fZ - oFromState.fZ;
			if (fDiffZ >= Math::PI) fDiffZ -= Math::TWO_PI;
			if (fDiffZ < -Math::PI) fDiffZ += Math::TWO_PI;
			fHistoryZ = oFromState.fZ + fDiffZ * (fTicks - fTimeAgo) / fTickTime;
		} else {
			fHistoryX = oStateHistory.front().fX;
			fHistoryY = oStateHistory.front().fY;
			fHistoryZ = oStateHistory.front().fZ;
		}
		if (fHistoryX == 0.0f || fHistoryY == 0.0f)
		{
			printf("Whoa %d %d %d %f %f\n", iID, cCurrentCommandSequenceNumber, cHistoryPoint, fTimeAgo, fTicks);
		}
	}*/
	/*if (fTimeAgo == 0.0f) {
		fHistoryX = GetIntX();
		fHistoryY = GetIntY();
		fHistoryZ = GetZ();
	} else */
	if (oStateHistory.size() == 0) {
		fHistoryX = oOnlyKnownState.fX;
		fHistoryY = oOnlyKnownState.fY;
		fHistoryZ = oOnlyKnownState.fZ;
	} else if (oStateHistory.size() == 1) {
		fHistoryX = oStateHistory.back().oState.fX;
		fHistoryY = oStateHistory.back().oState.fY;
		fHistoryZ = oStateHistory.back().oState.fZ;
	} else {
		float	fCurrentTimepoint = (u_char)(cCurrentCommandSequenceNumber - 1) + PlayerGet(iLocalPlayerID)->fTicks / fTickTime;
		float	fHistoryPoint = fCurrentTimepoint - fTimeAgo / fTickTime;
		if (fHistoryPoint < 0) fHistoryPoint += 256;
		int		nTicksAgo = (int)floorf(fTimeAgo / fTickTime - PlayerGet(iLocalPlayerID)->fTicks / fTickTime) + 1;
		//int		nTicksAgo = (int)(floorf(fTimeAgo / fTickTime - PlayerGet(iLocalPlayerID)->fTicks / fTickTime) + 0.1) + 1;
		char	cLastKnownTickAgo = (char)(cCurrentCommandSequenceNumber - oStateHistory.front().cSequenceNumber);
		list<SequencedState_t>::iterator oHistoryTo = oStateHistory.begin();
		list<SequencedState_t>::iterator oHistoryFrom = oStateHistory.begin(); ++oHistoryFrom;
		//if ((char)((u_char)floorf(fHistoryPoint) - oStateHistory.front().cSequenceNumber) >= 5)
		if (cLastKnownTickAgo > nTicksAgo)
		// Extrapolate into the future
		{
			//printf("future %d %d\n", cLastKnownTickAgo, nTicksAgo);
			/*fHistoryX = GetIntX();
			fHistoryY = GetIntY();
			fHistoryZ = GetZ();*/

			State_t oStateTo = oHistoryTo->oState;
			State_t oStateFrom = oHistoryFrom->oState;

			float fHistoryTicks = fHistoryPoint - oHistoryFrom->cSequenceNumber;
			if (fHistoryTicks < 0) fHistoryTicks += 256;
			if (fHistoryTicks > ((u_char)(oHistoryTo->cSequenceNumber - oHistoryFrom->cSequenceNumber) + PlayerGet(iLocalPlayerID)->fTicks + kfMaxExtrapolate) / fTickTime) {
				// Max forward extrapolation time
				fHistoryTicks = ((u_char)(oHistoryTo->cSequenceNumber - oHistoryFrom->cSequenceNumber) + PlayerGet(iLocalPlayerID)->fTicks + kfMaxExtrapolate) / fTickTime;

				printf("Exceeding max EXTERP time (player %d).\n", iID);
				printf("cur state = %d; last known state = %d\n", cCurrentCommandSequenceNumber, oStateHistory.front().cSequenceNumber);
				printf("cLastKnownTickAgo %d > nTicksAgo %d\n", cLastKnownTickAgo, nTicksAgo);
			}// else
			//	printf("Exterping into the future (player %d).\n", iID);

			u_char cHistoryTickTime = (u_char)(oHistoryTo->cSequenceNumber - oHistoryFrom->cSequenceNumber);
			fHistoryX = oStateFrom.fX + (oStateTo.fX - oStateFrom.fX) * fHistoryTicks / cHistoryTickTime;
			fHistoryY = oStateFrom.fY + (oStateTo.fY - oStateFrom.fY) * fHistoryTicks / cHistoryTickTime;

			float fDiffZ = oStateTo.fZ - oStateFrom.fZ;
			if (fDiffZ >= Math::PI) fDiffZ -= Math::TWO_PI;
			if (fDiffZ < -Math::PI) fDiffZ += Math::TWO_PI;
			if (fHistoryTicks > 1.0f) fHistoryTicks = sqrtf(fHistoryTicks);
			fHistoryZ = oStateFrom.fZ + fDiffZ * fHistoryTicks / cHistoryTickTime;
		}
		/*else if ((char)((u_char)floorf(fHistoryPoint) - oStateHistory.back().cSequenceNumber) < 0)
		// DEBUG: Need a better way to figure out if outside history range (for > 256 values
		// Way in the past, not enough history to interpolate
		{
			fHistoryX = oStateHistory.back().oState.fX;
			fHistoryY = oStateHistory.back().oState.fY;
			fHistoryZ = oStateHistory.back().oState.fZ;
		}*/
		else
		// Use the known history to interpolate
		{
			//bool bUseHistory = true;
			u_char cPeriodLength = 0;
			nTicksAgo -= cLastKnownTickAgo;
			while (oHistoryFrom != oStateHistory.end() &&
				//(char)((u_char)floorf(fHistoryPoint) - oHistoryFrom->cSequenceNumber) < 0) {
				nTicksAgo >= (cPeriodLength = (u_char)(oHistoryTo->cSequenceNumber - oHistoryFrom->cSequenceNumber))) {
				/*if (oHistoryFrom == oStateHistory.end()) {
					fHistoryX = oStateHistory.back().oState.fX;
					fHistoryY = oStateHistory.back().oState.fY;
					fHistoryZ = oStateHistory.back().oState.fZ;
					bUseHistory = false;
					break;
				} else {
					++oHistoryTo;
					++oHistoryFrom;
				}*/
				nTicksAgo -= cPeriodLength;
				++oHistoryTo;
				++oHistoryFrom;
			}

			if (oHistoryFrom == oStateHistory.end()) {
				fHistoryX = oStateHistory.back().oState.fX;
				fHistoryY = oStateHistory.back().oState.fY;
				fHistoryZ = oStateHistory.back().oState.fZ;
			} else {
				eX0_assert(oHistoryFrom->cSequenceNumber != oHistoryTo->cSequenceNumber, "dup!");
				if (oHistoryFrom->cSequenceNumber < oHistoryTo->cSequenceNumber)
				{
					/*eX0_assert(oHistoryFrom->cSequenceNumber <= fHistoryPoint
						&& fHistoryPoint <= oHistoryTo->cSequenceNumber,
						"not in seq");*/
					if (!(oHistoryFrom->cSequenceNumber <= fHistoryPoint
						&& fHistoryPoint <= oHistoryTo->cSequenceNumber))
					printf("%d <= %f <= %d\n", oHistoryFrom->cSequenceNumber,
											   fHistoryPoint,
											   oHistoryTo->cSequenceNumber);
					//else
					//	printf("%d <= %f <= %d ALL OK\n", oHistoryFrom->cSequenceNumber,
					//						   fHistoryPoint,
					//						   oHistoryTo->cSequenceNumber);
				}

				State_t oStateTo = oHistoryTo->oState;
				State_t oStateFrom = oHistoryFrom->oState;

				//float fHistoryTicks = fHistoryPoint - oHistoryFrom->cSequenceNumber;
				float fHistoryTicks = fHistoryPoint - oHistoryFrom->cSequenceNumber;
				if (fHistoryTicks < 0) fHistoryTicks += 256;
				u_char cHistoryTickTime = (u_char)(oHistoryTo->cSequenceNumber - oHistoryFrom->cSequenceNumber);
				//eX0_assert(fHistoryTicks < cHistoryTickTime, "fHistoryTicks not in range");
				if (!(fHistoryTicks <= cHistoryTickTime))
					printf("fHistoryTicks not in range: %f\n", fHistoryTicks / cHistoryTickTime);
				//else
				//	printf("fHistoryTicks IS OK: %f\n", fHistoryTicks / cHistoryTickTime);
				fHistoryX = oStateFrom.fX + (oStateTo.fX - oStateFrom.fX) * fHistoryTicks / cHistoryTickTime;
				fHistoryY = oStateFrom.fY + (oStateTo.fY - oStateFrom.fY) * fHistoryTicks / cHistoryTickTime;

				float fDiffZ = oStateTo.fZ - oStateFrom.fZ;
				if (fDiffZ >= Math::PI) fDiffZ -= Math::TWO_PI;
				if (fDiffZ < -Math::PI) fDiffZ += Math::TWO_PI;
				fHistoryZ = oStateFrom.fZ + fDiffZ * fHistoryTicks / cHistoryTickTime;
			}
		}
	}

	State_t oState;
	oState.fX = fHistoryX;
	oState.fY = fHistoryY;
	oState.fZ = fHistoryZ;
	return oState;
}

void CPlayer::RenderInPast(float fTimeAgo)
{
	State_t oState = GetStateInPast(fTimeAgo);

	// select player color
	if (IsDead())
		glColor3f(0.1f, 0.1f, 0.1f);
	else if (iTeam == 0)
		glColor3f(1, 0, 0);
	else if (iTeam == 1)
		glColor3f(0, 0, 1);
	else
		glColor3f(0, 1, 0);

	if (iID == iLocalPlayerID)
		glColor3f(0.5f, 0.5f, 0.5f);

	if (bWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_LINE);
	glPushMatrix();
	if (iID == iLocalPlayerID) OglUtilsSetMaskingMode(NO_MASKING_MODE);
	RenderOffsetCamera(false);
	glTranslatef(oState.fX, oState.fY, 0);
	glRotatef(oState.fZ * Math::RAD_TO_DEG, 0, 0, -1);
	glBegin(GL_QUADS);
		glVertex2i(-1, 11);
		glVertex2i(-1, 3);
		glVertex2i(1, 3);
		glVertex2i(1, 11);
	glEnd();
	gluPartialDisk(oQuadricObj, 6, 8, 12, 1, 30.0, 300.0);
	if (iID == iLocalPlayerID) OglUtilsSetMaskingMode(WITH_MASKING_MODE);
	glPopMatrix();
	if (bWireframe) glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);
}

void CPlayer::InitWeapons()
{
	oWeapons[0].Init(iID, 0);
	oWeapons[1].Init(iID, 0);
	oWeapons[2].Init(iID, 1);
	oWeapons[3].Init(iID, 2);
}

void CPlayer::UpdateInterpolatedPos()
{
	// is the player dead?
	if (IsDead()) {printf("assertion failed\n");return;}

	fIntX = fOldX + (fX - fOldX) * fTicks / fTickTime;
	fIntY = fOldY + (fY - fOldY) * fTicks / fTickTime;
	//fIntX = fX;
	//fIntY = fY;
}

string & CPlayer::GetName(void) { return sName; }
void CPlayer::SetName(string &sNewName) {
	if (sNewName.length() > 0 && sNewName.length() <= 32)
		sName = sNewName;
}

// allocate memory for all the players
void PlayerInit()
{
	for (int iLoop1 = 0; iLoop1 < 32; iLoop1++)
	{
		if (oPlayers[iLoop1] != NULL) delete oPlayers[iLoop1];

		if (iLoop1 < nPlayerCount)
		{
			oPlayers[iLoop1] = new CPlayer();
			oPlayers[iLoop1]->iID = iLoop1;
			oPlayers[iLoop1]->InitWeapons();
			oPlayers[iLoop1]->UpdateInterpolatedPos();
		}
	}
}

// Returns a player
CPlayer * PlayerGet(int nPlayerID)
{
	if (nPlayerID >= 0 &&nPlayerID < nPlayerCount) return oPlayers[nPlayerID];
	else return NULL;
}

void PlayerTick()
{
	for (int iLoop1 = 0; iLoop1 < nPlayerCount; ++iLoop1)
	{
		if (oPlayers[iLoop1]->bConnected)
			oPlayers[iLoop1]->Tick();
	}

	// update local player interpolated pos
	//oPlayers[iLocalPlayerID]->UpdateInterpolatedPos();
}

int PlayerGetFreePlayerID()
{
	for (int iLoop1 = 0; iLoop1 < nPlayerCount; ++iLoop1)
	{
		// Check if we've found a free player ID
		if (!oPlayers[iLoop1]->bConnected)
		{
			return iLoop1;
		}
	}

	return -1;		// No free player IDs; server is full
}