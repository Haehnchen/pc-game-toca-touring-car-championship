#pragma once

#include "scene/championship_finale.hpp"
#include "scene/view.hpp"

namespace toca::scene {
// Presentation copies only: authoritative simulation, visibility and audio keep
// their current tick values. Callers reset history at cuts, pauses and restarts.
bool camera_continuous(const CameraInputs& previous, const CameraInputs& current);
CameraInputs interpolate_camera(const CameraInputs& previous, const CameraInputs& current, float alpha);
CarPose interpolate_car_pose(const CarPose& previous, const CarPose& current, float alpha);
// Credit pages, teams and phase remain current. A phase/authored-camera cut
// presents the current frame immediately, matching the normal race history.
ChampionshipFinaleFrame interpolate_championship_finale(const ChampionshipFinaleFrame& previous, const ChampionshipFinaleFrame& current, float alpha);
} // namespace toca::scene
