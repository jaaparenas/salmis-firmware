#ifndef MEDIA_POLICY_H_
#define MEDIA_POLICY_H_

#include <string>

namespace media {

// The device plays music from its own storage, so the local library is the
// only music source. The cloud also has a music search of its own and pushes
// what it finds as a "notify" message with a remote URL; this policy refuses
// that audio so the robot never streams music from the cloud.
//
// The check never blocks: it only decides whether the cloud audio is played.
// Cloud audio is allowed when the device has no local library, so a robot
// without a card is not left mute. Set the NVS key "media" / "allow_remote"
// to true to always allow cloud audio instead.
bool ShouldRefuseCloudAudio(const std::string& audio_url);

}  // namespace media

#endif  // MEDIA_POLICY_H_
