///////////////////////////////////////////////////////
// Beehive: A complete SEGA Mega Drive content tool
//
// (c) 2016 Matt Phillips, Big Evil Corporation
// http://www.bigevilcorporation.co.uk
// mattphillips@mail.com
// @big_evil_corp
//
// Licensed under GPLv3, see http://www.gnu.org/licenses/gpl-3.0.html
///////////////////////////////////////////////////////

#include "TimelinePanel.h"
#include "KeyframePanel.h"
#include "MainWindow.h"

#include <ion/core/time/Time.h>

#include <wx/menu.h>

#include <sstream>

const char* TimelinePanel::s_trackNames[eTrackCount] =
{
	"Position",
	"Sprite Anim"
};

namespace
{
	template <typename T> float GetKeyframLength(Animation& animation, int index, ion::render::AnimationTrack<T>& track)
	{
		if(index < track.GetNumKeyframes())
		{
			float nextTime = (index + 1 < track.GetNumKeyframes()) ? track.GetKeyframe(index + 1).GetTime() : animation.GetLength();
			return nextTime - track.GetKeyframe(index).GetTime();
		}

		return 0.0f;
	}

	template <typename T> void AddOrEditKeyframe(const T& value, ion::render::AnimationTrack<T>& track, float frame)
	{
		if(ion::render::Keyframe<T>* keyframe = track.FindKeyframe(frame))
		{
			keyframe->SetValue(value);
		}
		else
		{
			track.InsertKeyframe(ion::render::Keyframe<T>(frame, value));
		}
	}

	void KeyframeObjectCurrentState(Project& project, AnimationActor& actor, float frame, int trackMask)
	{
		if(GameObject* gameObject = project.GetEditingMap().GetGameObject(actor.GetGameObjectId()))
		{
			//Add/edit keyframes for selected tracks
			if(trackMask & (1 << eTrackPosition))
			{
				AddOrEditKeyframe(gameObject->GetPosition(), actor.m_trackPosition, frame);
			}

			if(trackMask & (1 << eTrackSpriteAnim))
			{
				AddOrEditKeyframe(std::make_pair(gameObject->GetSpriteSheetId(), gameObject->GetSpriteAnim()), actor.m_trackSpriteAnim, frame);
			}
		}
	}
}

TimelinePanel::TimelinePanel(MainWindow& mainWindow, Project& project, ion::render::Renderer& renderer, wxGLContext& glContext, RenderResources& renderResources, wxWindow *parent, wxWindowID winid, const wxPoint& pos, const wxSize& size, long style, const wxString& name)
	: TimelinePanelBase(parent, winid, pos, size, style)
	, m_mainWindow(mainWindow)
	, m_project(project)
	, m_timer(this)
{
	m_animationId = InvalidAnimationId;
	m_animation = NULL;
	m_selectedActorId = InvalidActorId;
	m_selectedActor = NULL;
	m_selectedTrackId = eTrackPosition;
	m_prevClock = 0;

	PopulateAnimations();

	Bind(wxEVT_TIMER, &TimelinePanel::EventHandlerTimer, this, m_timer.GetId());
	Bind(wxEVT_SIZE, &TimelinePanel::OnResize, this, GetId());

	//Keyframe panel events
	m_timeline->Bind(EVT_TIME_CHANGED, TimeEventHandler(TimelinePanel::EventHandlerTimeline), this, wxID_ANY);
	m_timeline->Bind(EVT_TRACK_SELECTED, TrackEventHandler(TimelinePanel::EventHandlerTrack), this, wxID_ANY);
	m_timeline->Bind(EVT_KEYFRAME_SELECTED, KeyframeEventHandler(TimelinePanel::EventHandlerKeyframe), this, wxID_ANY);
	m_timeline->Bind(EVT_KEYFRAME_INSERTED, KeyframeEventHandler(TimelinePanel::EventHandlerKeyframe), this, wxID_ANY);
	m_timeline->Bind(EVT_KEYFRAME_DELETED, KeyframeEventHandler(TimelinePanel::EventHandlerKeyframe), this, wxID_ANY);
	m_timeline->Bind(EVT_KEYFRAME_MOVED, KeyframeEventHandler(TimelinePanel::EventHandlerKeyframe), this, wxID_ANY);
	m_timeline->Bind(EVT_KEYFRAME_RESIZED, KeyframeEventHandler(TimelinePanel::EventHandlerKeyframe), this, wxID_ANY);

	m_timer.Start(1);
}

TimelinePanel::~TimelinePanel()
{

}

void TimelinePanel::EventHandlerTimer(wxTimerEvent& event)
{
	if(m_animation && m_animation->GetState() == ion::render::Animation::ePlaying)
	{
		//Get accurate delta
		u64 clock = ion::time::GetSystemTicks();
		u64 diff = clock - m_prevClock;
		m_prevClock = clock;
		float delta = ion::time::TicksToSeconds(diff);

		m_animation->Update(delta);
		float time = m_animation->GetFrame();
		SetSliderFrame(time);

		if(m_toolIsolateObject->IsToggled() && m_selectedActor)
		{
			SyncActor(*m_selectedActor);
		}
		else
		{
			SyncAllActors();
		}
	}
}

void TimelinePanel::EventHandlerTimeline(TimeEvent& event)
{
	if(event.GetEventType() == EVT_TIME_CHANGED)
	{
		m_animation->SetFrame(event.m_time);

		if(m_toolIsolateObject->IsToggled() && m_selectedActor)
		{
			SyncActor(*m_selectedActor);
		}
		else
		{
			SyncAllActors();
		}
	}
}

void TimelinePanel::EventHandlerTrack(TrackEvent& event)
{
	if(event.GetEventType() == EVT_TRACK_SELECTED)
	{
		std::map<KeyframePanel::TrackId, TrackCache>::iterator it = m_trackCache.find(event.m_trackId);
		if(it != m_trackCache.end())
		{
			m_selectedActor = it->second.actor;
			m_selectedActorId = m_selectedActor->GetGameObjectId();
			m_selectedTrackId = it->second.trackType;
		}
	}
}

void TimelinePanel::EventHandlerKeyframe(KeyframeEvent& event)
{
	if(event.GetEventType() == EVT_KEYFRAME_SELECTED)
	{
	}
	else if(event.GetEventType() == EVT_KEYFRAME_INSERTED)
	{
	}
	else if(event.GetEventType() == EVT_KEYFRAME_DELETED)
	{
		KeyframeDeleted(event.m_keyframeId);
	}
	else if(event.GetEventType() == EVT_KEYFRAME_MOVED)
	{
		KeyframeModified(event.m_keyframeId, event.m_time, event.m_length);
	}
	else if(event.GetEventType() == EVT_KEYFRAME_RESIZED)
	{
		KeyframeModified(event.m_keyframeId, event.m_time, event.m_length);
	}
}

void TimelinePanel::KeyframeModified(KeyframePanel::KeyframeId keyframeId, float time, float length)
{
	{
		std::map<KeyframePanel::KeyframeId, KeyframeCache<ion::Vector2i>>::iterator it = m_keyframeCachePosition.find(keyframeId);
		if(it != m_keyframeCachePosition.end())
		{
			int keyframeIdx = it->second.keyframeIdx;

			//Remove and reinsert to support keyframes changing order
			AnimKeyframePosition keyframe = it->second.actor->m_trackPosition.GetKeyframe(keyframeIdx);
			it->second.actor->m_trackPosition.RemoveKeyframe(keyframeIdx);
			keyframe.SetTime(time);
			it->second.actor->m_trackPosition.InsertKeyframe(keyframe);

			RecalculateLenth(*m_animation);
			PopulateTimeline(*m_animation, m_selectedActor);
		}
	}
	
	{
		std::map<KeyframePanel::KeyframeId, KeyframeCache<std::pair<SpriteSheetId, SpriteAnimId>>>::iterator it = m_keyframeCacheSpriteAnim.find(keyframeId);
		if(it != m_keyframeCacheSpriteAnim.end())
		{
			int keyframeIdx = it->second.keyframeIdx;

			//Remove and reinsert to support keyframes changing order
			AnimKeyframeSpriteAnim& keyframe = it->second.actor->m_trackSpriteAnim.GetKeyframe(keyframeIdx);
			it->second.actor->m_trackSpriteAnim.RemoveKeyframe(keyframeIdx);
			keyframe.SetTime(time);
			it->second.actor->m_trackSpriteAnim.InsertKeyframe(keyframe);

			RecalculateLenth(*m_animation);
			PopulateTimeline(*m_animation, m_selectedActor);
		}
	}
}

void TimelinePanel::KeyframeDeleted(KeyframePanel::KeyframeId keyframeId)
{
	{
		std::map<KeyframePanel::KeyframeId, KeyframeCache<ion::Vector2i>>::iterator it = m_keyframeCachePosition.find(keyframeId);
		if(it != m_keyframeCachePosition.end())
		{
			int keyframeIdx = it->second.keyframeIdx;
			AnimKeyframePosition keyframe = it->second.actor->m_trackPosition.GetKeyframe(keyframeIdx);
			it->second.actor->m_trackPosition.RemoveKeyframe(keyframeIdx);

			RecalculateLenth(*m_animation);
			PopulateTimeline(*m_animation, m_selectedActor);
		}
	}

	{
		std::map<KeyframePanel::KeyframeId, KeyframeCache<std::pair<SpriteSheetId, SpriteAnimId>>>::iterator it = m_keyframeCacheSpriteAnim.find(keyframeId);
		if(it != m_keyframeCacheSpriteAnim.end())
		{
			int keyframeIdx = it->second.keyframeIdx;
			AnimKeyframeSpriteAnim& keyframe = it->second.actor->m_trackSpriteAnim.GetKeyframe(keyframeIdx);
			it->second.actor->m_trackSpriteAnim.RemoveKeyframe(keyframeIdx);

			RecalculateLenth(*m_animation);
			PopulateTimeline(*m_animation, m_selectedActor);
		}
	}
}

void TimelinePanel::SetSliderFrame(float frame)
{
	m_timeline->SetTime(frame);
}

void TimelinePanel::PopulateAnimations()
{
	m_choiceAnims->Clear();
	m_animCache.clear();

	typedef std::pair<std::string, AnimationId> TNameIDPair;
	typedef std::vector<TNameIDPair> TNameList;
	TNameList nameList;

	for(TAnimationMap::const_iterator it = m_project.AnimationsBegin(), end = m_project.AnimationsEnd(); it != end; ++it)
	{
		nameList.push_back(std::make_pair(it->second.GetName(), it->first));
	}

	std::sort(nameList.begin(), nameList.end(), [](TNameIDPair& a, TNameIDPair& b) { return a.first < b.first; });

	for(int i = 0; i < nameList.size(); i++)
	{
		//Store by index
		m_animCache.push_back(nameList[i].second);

		//Add to list
		m_choiceAnims->AppendString(nameList[i].first);
	}
}

void TimelinePanel::PopulateTimeline(Animation& animation, const AnimationActor* actor)
{
	//Set drop-down
	std::vector<AnimationId>::iterator it = std::find_if(m_animCache.begin(), m_animCache.end(), [&](const AnimationId& animId) { return animId == animation.GetId(); });
	int index = std::distance(it, m_animCache.begin());

	if(m_choiceAnims->GetSelection() != index)
	{
		m_choiceAnims->SetSelection(index);
	}

	m_timeline->Clear();
	m_timeline->SetLength(animation.GetLength());

	m_keyframeCachePosition.clear();
	m_keyframeCacheSpriteAnim.clear();

	for(TAnimActorMap::iterator actorIt = animation.ActorsBegin(), actorEnd = animation.ActorsEnd(); actorIt != actorEnd; ++actorIt)
	{
		if(GameObject* gameObject = m_project.GetEditingMap().GetGameObject(actorIt->second.GetGameObjectId()))
		{
			if(const GameObjectType* gameObjectType = m_project.GetGameObjectType(gameObject->GetTypeId()))
			{
				KeyframePanel::SectionId sectionId = m_timeline->AddSection(gameObject->GetName());

				KeyframePanel::TrackId positionTrackId = m_timeline->AddTrack(sectionId, "Position");
				KeyframePanel::TrackId spriteAnimTrackId = m_timeline->AddTrack(sectionId, "Sprite Anim");

				m_trackCache.insert(std::make_pair(positionTrackId, TrackCache({ &actorIt->second, gameObject, eTrackPosition })));
				m_trackCache.insert(std::make_pair(spriteAnimTrackId, TrackCache({ &actorIt->second, gameObject, eTrackSpriteAnim })));

				for(int i = 0; i < actorIt->second.m_trackPosition.GetNumKeyframes(); i++)
				{
					AnimKeyframePosition& keyframe = actorIt->second.m_trackPosition.GetKeyframe(i);
					float time = keyframe.GetTime();
					float length = GetKeyframLength(animation, i, actorIt->second.m_trackPosition);
					std::stringstream label;
					label << "(" << keyframe.GetValue().x << "," << keyframe.GetValue().y << ")";
					KeyframePanel::KeyframeId keyframeId = m_timeline->AddKeyframe(sectionId, positionTrackId, time, length, label.str());
					m_keyframeCachePosition.insert(std::make_pair(keyframeId, KeyframeCache<ion::Vector2i>({ &actorIt->second, gameObject, &actorIt->second.m_trackPosition, i })));
				}

				for(int i = 0; i < actorIt->second.m_trackSpriteAnim.GetNumKeyframes(); i++)
				{
					AnimKeyframeSpriteAnim& keyframe = actorIt->second.m_trackSpriteAnim.GetKeyframe(i);
					float time = keyframe.GetTime();
					float length = GetKeyframLength(animation, i, actorIt->second.m_trackSpriteAnim);

					std::stringstream label;
					std::pair<SpriteSheetId, SpriteAnimId> value = actorIt->second.m_trackSpriteAnim.GetKeyframe(i).GetValue();
					if(Actor* spriteActor = m_project.GetActor(gameObjectType->GetSpriteActorId()))
					{
						if(SpriteSheet* spriteSheet = spriteActor->GetSpriteSheet(value.first))
						{
							if(SpriteAnimation* spriteAnim = spriteSheet->GetAnimation(value.second))
							{
								label << spriteSheet->GetName() << ":" << spriteAnim->GetName();
							}
						}
					}

					KeyframePanel::KeyframeId keyframeId = m_timeline->AddKeyframe(sectionId, spriteAnimTrackId, time, length, label.str());
					m_keyframeCacheSpriteAnim.insert(std::make_pair(keyframeId, KeyframeCache<std::pair<SpriteSheetId, SpriteAnimId>>({ &actorIt->second, gameObject, &actorIt->second.m_trackSpriteAnim, i })));
				}
			}
		}
	}

	m_timeline->Refresh();
}

void TimelinePanel::SetCurrentActor(GameObjectId actorId)
{
	if(actorId != m_selectedActorId)
	{
		if(const GameObject* gameObject = m_project.GetEditingMap().GetGameObject(actorId))
		{
			m_selectedActorId = actorId;

			if(m_animation)
			{
				m_selectedActor = m_animation->GetActor(actorId);

				if(m_selectedActor)
				{
					//Populate timeline
					PopulateTimeline(*m_animation, m_selectedActor);

					//Set game object label
					std::stringstream label;

					std::string objName = gameObject->GetName();
					if(objName.empty())
					{
						objName = "<No Name>";
					}

					label << objName << " (object " << gameObject->GetId() << ")";
					m_textCurrentActor->SetLabel(label.str());

					//Populate sprite actor choices
					m_choiceSpriteAnim->Clear();
					m_spriteSheetCache.clear();

					if(const GameObjectType* gameObjectType = m_project.GetGameObjectType(gameObject->GetTypeId()))
					{
						ActorId spriteActorId = gameObjectType->GetSpriteActorId();
						if(Actor* spriteActor = m_project.GetActor(spriteActorId))
						{
							for(TSpriteSheetMap::iterator spriteSheetIt = spriteActor->SpriteSheetsBegin(), spriteSheetEnd = spriteActor->SpriteSheetsEnd(); spriteSheetIt != spriteSheetEnd; ++spriteSheetIt)
							{
								for(TSpriteAnimMap::iterator animIt = spriteSheetIt->second.AnimationsBegin(), animEnd = spriteSheetIt->second.AnimationsEnd(); animIt != animEnd; ++animIt)
								{
									std::stringstream label;
									label << spriteSheetIt->second.GetName() << ":" << animIt->second.GetName();
									m_choiceSpriteAnim->AppendString(label.str());
									m_spriteSheetCache.push_back(std::make_pair(spriteSheetIt->first, animIt->first));
								}
							}
						}
					}
				}
			}
		}
	}
}

AnimationId TimelinePanel::GetCurrentAnimation() const
{
	return m_animationId;
}

void TimelinePanel::SyncActor(AnimationActor& actor)
{
	if(m_animation)
	{
		if(GameObject* gameObject = m_project.GetEditingMap().GetGameObject(actor.GetGameObjectId()))
		{
			float frame = m_animation->GetFrame();
			SpriteAnimId originalSpriteAnim = gameObject->GetSpriteAnim();

			//Apply all track values to object
			if(actor.m_trackPosition.GetNumKeyframes() > 0)
			{
				ion::Vector2i position = actor.m_trackPosition.GetValue(frame);
				m_project.GetEditingMap().MoveGameObject(actor.GetGameObjectId(), position.x, position.y);
				gameObject->SetPosition(position);
			}

			if(actor.m_trackSpriteAnim.GetNumKeyframes() > 0)
			{
				std::pair<SpriteSheetId, SpriteAnimId> spriteAnim = actor.m_trackSpriteAnim.GetValue(frame);
				gameObject->SetSpriteSheetId(spriteAnim.first);
				gameObject->SetSpriteAnim(spriteAnim.second);

				//Advance current sprite anim
				if(const GameObjectType* gameObjectType = m_project.GetGameObjectType(gameObject->GetTypeId()))
				{
					if(Actor* spriteActor = m_project.GetActor(gameObjectType->GetSpriteActorId()))
					{
						if(SpriteSheet* spriteSheet = spriteActor->GetSpriteSheet(spriteAnim.first))
						{
							if(SpriteAnimation* spriteAnimation = spriteSheet->GetAnimation(spriteAnim.second))
							{
								float spriteAnimStartTime = actor.m_trackSpriteAnim.GetPrevKeyframe(frame)->GetTime();
								float spriteAnimFrame = (frame - spriteAnimStartTime) * spriteAnimation->GetPlaybackSpeed();
								float spriteAnimLength = spriteAnimation->GetLength();

								if(spriteAnimation->GetPlaybackBehaviour() == ion::render::Animation::eLoop)
								{
									spriteAnimation->SetFrame(ion::maths::Fmod(spriteAnimFrame, spriteAnimLength));
								}
								else
								{
									spriteAnimation->SetFrame(ion::maths::Clamp(spriteAnimFrame, 0.0f, spriteAnimLength));
								}
							}
						}
					}
				}
			}

			//Refresh map panel
			m_mainWindow.RedrawPanel(MainWindow::ePanelMap);
		}
	}
}

void TimelinePanel::SyncAllActors()
{
	if(m_animation)
	{
		for(TAnimActorMap::iterator it = m_animation->ActorsBegin(), end = m_animation->ActorsEnd(); it != end; ++it)
		{
			SyncActor(it->second);
		}
	}
}

void TimelinePanel::RecalculateLenth(Animation& animation)
{
	float length = animation.GetLength();

	for(TAnimActorMap::iterator actorIt = animation.ActorsBegin(), actorEnd = animation.ActorsEnd(); actorIt != actorEnd; ++actorIt)
	{
		if(GameObject* gameObject = m_project.GetEditingMap().GetGameObject(actorIt->second.GetGameObjectId()))
		{
			if(const GameObjectType* gameObjectType = m_project.GetGameObjectType(gameObject->GetTypeId()))
			{
				float lengthPosTrack = actorIt->second.m_trackPosition.GetLength() + 1.0f;

				if(lengthPosTrack > length)
				{
					length = lengthPosTrack;
				}

				float lengthSpriteAnimTrack = actorIt->second.m_trackSpriteAnim.GetLength() + 1.0f;

				if(lengthSpriteAnimTrack > length)
				{
					length = lengthSpriteAnimTrack;
				}
			}
		}
	}

	animation.SetLength(length);
	m_timeline->SetLength(length);
}

void TimelinePanel::Refresh(bool eraseBackground, const wxRect *rect)
{
	if(!m_mainWindow.IsRefreshLocked())
	{
	}
}

void TimelinePanel::OnSpinSpeed(wxSpinEvent& event)
{
	if(m_animation)
	{
		m_animation->SetPlaybackSpeed((float)m_spinSpeed->GetValue());
	}
}

void TimelinePanel::OnSelectAnimation(wxCommandEvent& event)
{
	int index = event.GetSelection();
	m_animationId = m_animCache[index];
	m_animation = m_project.GetAnimation(m_animationId);
	if(m_animation)
	{
		m_animation->SetState(ion::render::Animation::eStopped);
		PopulateTimeline(*m_animation, NULL);
	}
}

void TimelinePanel::OnSelectSpriteAnim(wxCommandEvent& event)
{
	if(m_animation && m_selectedActor)
	{
		if(GameObject* gameObject = m_project.GetEditingMap().GetGameObject(m_selectedActor->GetGameObjectId()))
		{
			gameObject->SetSpriteSheetId(m_spriteSheetCache[m_choiceSpriteAnim->GetSelection()].first);
			gameObject->SetSpriteAnim(m_spriteSheetCache[m_choiceSpriteAnim->GetSelection()].second);

			PopulateTimeline(*m_animation, m_selectedActor);
			m_mainWindow.RedrawPanel(MainWindow::ePanelMap);
		}
	}
}

void TimelinePanel::OnToolAddAnim(wxCommandEvent& event)
{
	DialogNewAnim dialog(this);

	if(dialog.ShowModal() == wxID_OK)
	{
		m_animationId = m_project.CreateAnimation();
		m_animation = m_project.GetAnimation(m_animationId);
		m_animation->SetName(dialog.m_textName->GetValue().GetData().AsChar());
		PopulateAnimations();
		PopulateTimeline(*m_animation, NULL);
	}
}

void TimelinePanel::OnToolDeleteAnim(wxCommandEvent& event)
{
	if(m_animation)
	{
		m_project.DeleteAnimation(m_animationId);
		m_animationId = InvalidAnimationId;
		m_animation = NULL;
		m_selectedActorId = InvalidActorId;

		PopulateAnimations();
	}
}

void TimelinePanel::Keyframe(AnimationActor* actor, int trackMask)
{
	if(m_animation)
	{
		//Get current frame
		float frame = m_animation->GetFrame();

		if(actor)
		{
			//Edit or insert keyframe for current actor
			KeyframeObjectCurrentState(m_project, *actor, frame, trackMask);
		}
		else
		{
			//Edit or insert keyframe for all actors
			for(TAnimActorMap::iterator it = m_animation->ActorsBegin(), end = m_animation->ActorsEnd(); it != end; ++it)
			{
				KeyframeObjectCurrentState(m_project, it->second, frame, trackMask);
			}
		}

		//If at end, advance to next keyframe
		if(frame >= m_animation->GetLength() - 1.0f)
		{
			//Advance frame
			frame += 1.0f;

			//Set next frame
			m_animation->SetFrame(frame);

			//Update sider
			SetSliderFrame(frame);
		}

		//Adjust anim length
		RecalculateLenth(*m_animation);

		if(m_toolIsolateObject->IsToggled() && actor)
		{
			SyncActor(*actor);
		}
		else
		{
			SyncAllActors();
		}

		//Redraw map panel
		m_mainWindow.RedrawPanel(MainWindow::ePanelMap);

		//Populate timeline
		PopulateTimeline(*m_animation, m_selectedActor);
	}
}

void TimelinePanel::SetAnimLength(Animation& animation, const AnimationActor& actor)
{
	float length = 1.0f;

	for(int i = 0; i < actor.m_trackPosition.GetNumKeyframes(); i++)
	{
		length += actor.m_trackPosition.GetKeyframe(i).GetTime();
	}

	animation.SetLength(length);
}

void TimelinePanel::OnToolKeyframeTrack(wxCommandEvent& event)
{
	if(m_selectedActor)
	{
		Keyframe(m_selectedActor, (1 << m_selectedTrackId));
	}
}

void TimelinePanel::OnToolKeyframeActor(wxCommandEvent& event)
{
	if(m_selectedActor)
	{
		Keyframe(m_selectedActor);
	}
}

void TimelinePanel::OnToolKeyframeAll(wxCommandEvent& event)
{
	Keyframe(NULL);
}

void TimelinePanel::OnToolPlay(wxCommandEvent& event)
{
	if(m_animation)
	{
		m_animation->SetState(ion::render::Animation::ePlaying);
		m_prevClock = ion::time::GetSystemTicks();
	}
}

void TimelinePanel::OnToolStop(wxCommandEvent& event)
{
	if(m_animation)
	{
		m_animation->SetState(ion::render::Animation::eStopped);
	}
}

void TimelinePanel::OnToolRewind(wxCommandEvent& event)
{
	if(m_animation)
	{
		m_animation->SetFrame(0.0f);
	}

	SetSliderFrame(0.0f);

	if(m_toolIsolateObject->IsToggled() && m_selectedActor)
	{
		SyncActor(*m_selectedActor);
	}
	else
	{
		SyncAllActors();
	}

	m_mainWindow.RedrawPanel(MainWindow::ePanelMap);
}

void TimelinePanel::OnToolFastForward(wxCommandEvent& event)
{
	if(m_animation)
	{
		m_animation->SetFrame(m_animation->GetLength());
	}

	SetSliderFrame(m_animation->GetLength());

	if(m_toolIsolateObject->IsToggled() && m_selectedActor)
	{
		SyncActor(*m_selectedActor);
	}
	else
	{
		SyncAllActors();
	}

	m_mainWindow.RedrawPanel(MainWindow::ePanelMap);
}

void TimelinePanel::OnToolIsolateObject(wxCommandEvent& event)
{
	if(event.IsChecked() && !m_selectedActor)
	{
		m_toolIsolateObject->SetToggle(false);
	}
}

void TimelinePanel::OnResize(wxSizeEvent& event)
{
	event.Skip();
}

void TimelinePanel::OnContextMenuClick(wxCommandEvent& event)
{
	if(m_animation && m_selectedActor)
	{
		switch(event.GetId())
		{
			case eMenuInsertKeyframeLeft:
				break;
			case eMenuInsertKeyframeRight:
				break;
			case eMenuDeleteKeyframe:
				break;
			case eMenuSetSpriteAnimLength:
			{
				int keyframeIdx = m_contextMenuColIdx;

				if(m_selectedActor->m_trackSpriteAnim.GetNumKeyframes() > keyframeIdx + 1)
				{
					AnimKeyframeSpriteAnim& keyframe = m_selectedActor->m_trackSpriteAnim.GetKeyframe(keyframeIdx);
					SpriteSheetId spriteSheetId = keyframe.GetValue().first;
					SpriteAnimId spriteAnimId = keyframe.GetValue().second;

					if(GameObject* gameObject = m_project.GetEditingMap().GetGameObject(m_selectedActor->GetGameObjectId()))
					{
						if(const GameObjectType* gameObjectType = m_project.GetGameObjectType(gameObject->GetTypeId()))
						{
							if(Actor* spriteActor = m_project.GetActor(gameObjectType->GetSpriteActorId()))
							{
								if(SpriteSheet* spriteSheet = spriteActor->GetSpriteSheet(spriteSheetId))
								{
									if(SpriteAnimation* spriteAnim = spriteSheet->GetAnimation(spriteAnimId))
									{
										float spriteAnimLength = spriteAnim->GetLength() / spriteAnim->GetPlaybackSpeed();

										float prevTimeA = keyframe.GetTime();
										float prevTimeB = m_selectedActor->m_trackSpriteAnim.GetKeyframe(keyframeIdx + 1).GetTime();
										float prevLength = prevTimeB - prevTimeA;
										float delta = spriteAnimLength - prevLength;

										const float minTime = 0.01f;

										if(spriteAnimLength < minTime)
										{
											delta = spriteAnimLength = minTime;
										}

										//Shift all keyframes beyond current
										for(int i = keyframeIdx + 1; i < m_selectedActor->m_trackPosition.GetNumKeyframes(); i++)
										{
											m_selectedActor->m_trackPosition.GetKeyframe(i).SetTime(m_selectedActor->m_trackPosition.GetKeyframe(i).GetTime() + delta);
											m_selectedActor->m_trackSpriteAnim.GetKeyframe(i).SetTime(m_selectedActor->m_trackSpriteAnim.GetKeyframe(i).GetTime() + delta);
										}

										//Adjust anim length
										SetAnimLength(*m_animation, *m_selectedActor);

										//Redraw
										PopulateTimeline(*m_animation, m_selectedActor);
									}
								}
							}
						}
					}
				}

				break;
			}
		}
	}
}