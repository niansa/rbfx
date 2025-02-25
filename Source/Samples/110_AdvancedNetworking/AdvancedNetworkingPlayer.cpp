//
// Copyright (c) 2008-2020 the Urho3D project.
// Copyright (c) 2022 the rbfx project.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.
//

#include "AdvancedNetworkingPlayer.h"

#include <Urho3D/Graphics/Animation.h>
#include <Urho3D/Graphics/AnimationController.h>
#include <Urho3D/Physics/KinematicCharacterController.h>
#include <Urho3D/Replica/PredictedKinematicController.h>
#include <Urho3D/Replica/ReplicatedTransform.h>
#include <Urho3D/Resource/ResourceCache.h>

namespace
{

/// Return shortest angle between two angles.
float ShortestAngle(float lhs, float rhs)
{
    const float delta = Mod(rhs - lhs + 180.0f, 360.0f) - 180.0f;
    return delta < -180.0f ? delta + 360.0f : delta;
}

/// Helper function to smoothly transform one quaternion into another.
Quaternion TransformRotation(const Quaternion& base, const Quaternion& target, float maxAngularVelocity)
{
    const float baseYaw = base.YawAngle();
    const float targetYaw = target.YawAngle();
    if (Equals(baseYaw, targetYaw, 0.1f))
        return base;

    const float shortestAngle = ShortestAngle(baseYaw, targetYaw);
    const float delta = Sign(shortestAngle) * ea::min(Abs(shortestAngle), maxAngularVelocity);
    return Quaternion{baseYaw + delta, Vector3::UP};
}
}

AdvancedNetworkingPlayer::AdvancedNetworkingPlayer(Context* context)
    : ReplicatedAnimation(context, CallbackMask)
{
    auto cache = GetSubsystem<ResourceCache>();
    animations_ = {
        cache->GetResource<Animation>("Models/Mutant/Mutant_Idle0.ani"),
        cache->GetResource<Animation>("Models/Mutant/Mutant_Run.ani"),
        cache->GetResource<Animation>("Models/Mutant/Mutant_Jump1.ani"),
    };
}

void AdvancedNetworkingPlayer::InitializeOnServer()
{
    BaseClassName::InitializeOnServer();

    InitializeCommon();

    // On server, all players are unimportant because they are moving and need temporal raycasts
    auto animatedModel = GetComponent<Urho3D::AnimatedModel>();
    animatedModel->SetViewMask(UNIMPORTANT_VIEW_MASK);
}

void AdvancedNetworkingPlayer::InitializeFromSnapshot(
    Urho3D::NetworkFrame frame, Urho3D::Deserializer& src, bool isOwned)
{
    BaseClassName::InitializeFromSnapshot(frame, src, isOwned);

    InitializeCommon();

    // Mark all players except ourselves as important for raycast.
    auto animatedModel = GetComponent<AnimatedModel>();
    animatedModel->SetViewMask(isOwned ? UNIMPORTANT_VIEW_MASK : IMPORTANT_VIEW_MASK);

    // Setup client-side containers
    ReplicationManager* replicationManager = GetNetworkObject()->GetReplicationManager();
    const unsigned traceDuration = replicationManager->GetTraceDurationInFrames();

    animationTrace_.Resize(traceDuration);
    rotationTrace_.Resize(traceDuration);

    // Smooth rotation a bit, but without extrapolation
    rotationSampler_.Setup(0, 15.0f, M_LARGE_VALUE);
}

bool AdvancedNetworkingPlayer::PrepareUnreliableDelta(NetworkFrame frame)
{
    BaseClassName::PrepareUnreliableDelta(frame);

    return true;
}

void AdvancedNetworkingPlayer::WriteUnreliableDelta(NetworkFrame frame, Serializer& dest)
{
    BaseClassName::WriteUnreliableDelta(frame, dest);

    const ea::string& currentAnimationName = animations_[currentAnimation_]->GetName();
    const float currentTime = animationController_->GetTime(currentAnimationName);

    dest.WriteVLE(currentAnimation_);
    dest.WriteFloat(currentTime);
    dest.WriteQuaternion(node_->GetWorldRotation());
}

void AdvancedNetworkingPlayer::ReadUnreliableDelta(NetworkFrame frame, Deserializer& src)
{
    BaseClassName::ReadUnreliableDelta(frame, src);

    const unsigned animationIndex = src.ReadVLE();
    const float animationTime = src.ReadFloat();
    const Quaternion rotation = src.ReadQuaternion();

    animationTrace_.Set(frame, {animationIndex, animationTime});
    rotationTrace_.Set(frame, rotation);
}

void AdvancedNetworkingPlayer::InterpolateState(
    float timeStep, const NetworkTime& replicaTime, const NetworkTime& inputTime)
{
    BaseClassName::InterpolateState(timeStep, replicaTime, inputTime);

    if (!GetNetworkObject()->IsReplicatedClient())
        return;

    // Interpolate player rotation
    if (auto newRotation = rotationSampler_.UpdateAndSample(rotationTrace_, replicaTime, timeStep))
        node_->SetWorldRotation(*newRotation);

    // Extrapolate animation time from trace
    if (const auto animationSample = animationTrace_.GetRawOrPrior(replicaTime.Frame()))
    {
        const auto& [value, sampleFrame] = *animationSample;
        const auto& [animationIndex, animationTime] = value;

        // Just play most recent received animation
        Animation* animation = animations_[animationIndex];
        animationController_->PlayExclusive(animation->GetName(), 0, false, fadeTime_);

        // Adjust time to stay synchronized with server
        const float delay = (replicaTime - NetworkTime{sampleFrame}) * networkFrameDuration_;
        const float time = Mod(animationTime + delay, animation->GetLength());
        animationController_->SetTime(animation->GetName(), time);
    }
}

void AdvancedNetworkingPlayer::Update(float replicaTimeStep, float inputTimeStep)
{
    if (!GetNetworkObject()->IsReplicatedClient())
        UpdateAnimations(inputTimeStep);

    BaseClassName::Update(replicaTimeStep, inputTimeStep);
}

void AdvancedNetworkingPlayer::InitializeCommon()
{
    ReplicationManager* replicationManager = GetNetworkObject()->GetReplicationManager();
    networkFrameDuration_ = 1.0f / replicationManager->GetUpdateFrequency();

    // Resolve dependencies
    replicatedTransform_ = GetNetworkObject()->GetNetworkBehavior<ReplicatedTransform>();
    networkController_ = GetNetworkObject()->GetNetworkBehavior<PredictedKinematicController>();
    kinematicController_ = networkController_->GetComponent<KinematicCharacterController>();
}

void AdvancedNetworkingPlayer::UpdateAnimations(float timeStep)
{
    // Get current state of the controller to deduce animation from
    const bool isGrounded = kinematicController_->OnGround();
    const Vector3 velocity = networkController_->GetVelocity();
    const Vector3 walkDirection = Vector3::FromXZ(velocity.ToXZ().NormalizedOrDefault(Vector2::ZERO, 0.1f));

    // Start jump if has high vertical velocity and hasn't jumped yet
    if ((currentAnimation_ != ANIM_JUMP) && velocity.y_ > jumpThreshold_)
    {
        animationController_->PlayExclusive(animations_[ANIM_JUMP]->GetName(), 0, false, fadeTime_);
        animationController_->SetTime(animations_[ANIM_JUMP]->GetName(), 0);
        currentAnimation_ = ANIM_JUMP;
    }

    // Rotate player both on ground and in the air
    if (walkDirection != Vector3::ZERO)
    {
        const Quaternion targetRotation{Vector3::BACK, walkDirection};
        const float maxAngularVelocity = MAX_ROTATION_SPEED * timeStep;
        node_->SetWorldRotation(TransformRotation(node_->GetWorldRotation(), targetRotation, maxAngularVelocity));
    }

    // If on the ground, either walk or stay idle
    if (isGrounded)
    {
        currentAnimation_ = walkDirection != Vector3::ZERO ? ANIM_WALK : ANIM_IDLE;
        animationController_->PlayExclusive(animations_[currentAnimation_]->GetName(), 0, true, fadeTime_);
    }
}
