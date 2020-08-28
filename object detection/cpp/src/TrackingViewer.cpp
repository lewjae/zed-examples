#include "TrackingViewer.hpp"

// -------------------------------------------------
//            2D LEFT VIEW
// -------------------------------------------------
template<typename T>
inline cv::Point2f cvt(T pt, sl::float2 scale) {
    return cv::Point2f(pt.x * scale.x, pt.y * scale.y);
}

void render_2D(sl::Mat &left, sl::float2 img_scale, std::vector<sl::ObjectData> &objects, bool render_mask) {
    cv::Mat left_display = slMat2cvMat(left);
    cv::Mat overlay = left_display.clone();

    cv::Rect roi_render(0, 0, left_display.size().width, left_display.size().height);

    // render skeleton joints and bones
    for (const auto& obj : objects) {
        if(renderObject(obj)) {
            if (obj.keypoint_2d.size()) {
                cv::Scalar color = generateColorID_u(obj.id);
                // skeleton joints
                for (auto& kp : obj.keypoint_2d) {
                    cv::Point2f cv_kp = cvt(kp, img_scale);
                    if (roi_render.contains(cv_kp))
                        cv::circle(left_display, cv_kp, 4, color, -1);
                }
                // skeleton bones
                for (const auto& parts : sl::BODY_BONES) {
                    auto kp_a = cvt(obj.keypoint_2d[getIdx(parts.first)], img_scale);
                    auto kp_b = cvt(obj.keypoint_2d[getIdx(parts.second)], img_scale);
                    if (roi_render.contains(kp_a) && roi_render.contains(kp_b))
                        cv::line(left_display, kp_a, kp_b, color, 2);
                }          
            }
        }
    }

    cv::Mat cv_mask;
    // sort objects by depth
    std::sort(objects.begin(), objects.end(), [ ](const sl::ObjectData& obj1, const sl::ObjectData& obj2) { return obj1.position.z < obj2.position.z;});

    for (auto &obj : objects) {
        if(renderObject(obj)) {
            cv::Scalar base_color = generateColorID_u(obj.id);
            cv::Scalar faded_bbox_color = base_color;
            faded_bbox_color.val[3] = 0.;

            // New 2D bounding box
            cv::Point top_left_corner = cvt(obj.bounding_box_2d[0], img_scale);
            cv::Point top_right_corner = cvt(obj.bounding_box_2d[1], img_scale);
            cv::Point bottom_right_corner = cvt(obj.bounding_box_2d[2], img_scale);
            cv::Point bottom_left_corner = cvt(obj.bounding_box_2d[3], img_scale);

            // scaled ROI
            cv::Rect roi(top_left_corner, bottom_right_corner);

            // Creation of the 2 horizontal lines
            int bbox_thickness = 1;
            cv::line(left_display, top_left_corner, top_right_corner, base_color, bbox_thickness);
            cv::line(left_display, bottom_left_corner, bottom_right_corner, base_color, bbox_thickness);

            // Creation of two vertical lines
            // Left
            drawVerticalLine(left_display, bottom_left_corner, top_left_corner, base_color, faded_bbox_color, bbox_thickness);
            // Right
            drawVerticalLine(left_display, bottom_right_corner, top_right_corner, base_color, faded_bbox_color, bbox_thickness);

            auto position_image = getImagePosition(obj.bounding_box_2d, img_scale);

            if (obj.tracking_state == sl::OBJECT_TRACKING_STATE::OK && render_mask && obj.mask.isInit()) {
                // Faded object mask
                // Mask will only contain one object segmentation mask, that can be superposed with left display
                cv::Mat mask(left_display.rows, left_display.cols, CV_8UC1, cv::Scalar::all(0));
                // Here, obj.mask is the object segmentation mask inside the object bbox, computed on the native resolution
                // The resize is needed to get the mask on the display resolution
                cv::resize(slMat2cvMat(obj.mask), cv_mask, roi.size());
                // Here, we copy the mask into the mask variable
                cv_mask.copyTo(mask(roi));
                // Finally, we use mask to create an opaque mask on the overlay image, with the ID specific color
                overlay.setTo(base_color, mask);
            } else {
                cv::Mat mask(left_display.rows, left_display.cols, CV_8UC1, cv::Scalar::all(0));
                mask(roi) = 1;
                overlay.setTo(base_color, mask);
            }

            if (!std::isnan(obj.position.z)) {
                cv::Scalar text_color(255, 255, 255);
                std::string label_str = toString(obj.label).get();
                char text[64];
                sprintf(text, "%.2fm", abs(obj.position.z / 1000.0f));
                putText( left_display, text, cv::Point2d(position_image.x - 20, position_image.y - 12),
                        cv::FONT_HERSHEY_COMPLEX_SMALL, 0.5, text_color, 1 );
            }
        }
    }
    // Here, overlay is as the left image, but with opaque masks on each detected objects
    cv::addWeighted(left_display, 0.65, overlay, 0.35, 0.0, left_display);
}

// -------------------------------------------------------
//              Tracklet code
// -------------------------------------------------------

void Tracklet::addDetectedPoint(const sl::ObjectData obj, uint64_t timestamp, int smoothing_window_size) {
    if (positions.back().tracking_state == TrackPointState::PREDICTED || recovery_cpt < recovery_length) {
        if (positions.back().tracking_state == TrackPointState::PREDICTED) {
            recovery_cpt = 0;
        } else {
            ++recovery_cpt;
        }
    }

    positions.push_back(TrackPoint(obj.position, TrackPointState::OK, timestamp));
    tracking_state = obj.tracking_state;
    last_detected_timestamp = timestamp;

    positions_to_draw.push_back(TrackPoint(obj.position, TrackPointState::OK, timestamp));
}


// -------------------------------------------------------
//              TrackingViewer code
// -------------------------------------------------------

TrackingViewer::TrackingViewer() {
    // ----------- Default configuration -----------------
    // Object range
    x_min = -6250.0f;
    x_max = -x_min;
    z_min = -12500.0f;

    // window size
    window_width = 800;
    window_height = 800;

    // Visualization configuration
    end_of_track_color = cv::Scalar(255, 40, 40);
    camera_offset = 50;
    x_step = (x_max - x_min) / window_width;
    z_step = abs(z_min) / window_height;

    // history management
    min_length_to_draw = 3;

    // Configuration through FPS information
    fps = 30;
    configureFromFPS();

    // camera settings
    fov = -1.0f;


    // Visualization settings
    background_color = cv::Scalar(248, 248, 248);
#if (defined(CV_VERSION_EPOCH) && CV_VERSION_EPOCH == 2)
    cv::Scalar ref = cv::Scalar(255, 117, 44);
    for (int p = 0; p < 3; p++)
        fov_color.val[p] = (ref.val[p] + 2 * background_color.val[p]) / 3;
#else
    fov_color = (cv::Scalar(255, 117, 44) + 2 * background_color) / 3;
#endif
    has_background_ready = false;

    // SMOOTH
    do_smooth = false;
}

void TrackingViewer::setFPS(const int fps_, bool configure_all) {
    fps = fps_;
    frame_time_step = uint64_t(ceil(1000000000.0f / fps_));
    if (configure_all) {
        configureFromFPS();
    }
}

void TrackingViewer::configureFromFPS() {
    frame_time_step = uint64_t(ceil(1000000000.0f / fps));

    // Show last 1.5 seconds
    history_size = int(1.5f * fps);

    // Threshold to delete track
    max_missing_points = std::max(fps / 6, 4);

    // Smoothing window: 80ms
    smoothing_window_size = static_cast<int>(ceil(0.08f * fps) +.5f);
}

void TrackingViewer::generate_view(sl::Objects &objects, sl::Pose current_camera_pose, cv::Mat &tracking_view, bool tracking_enabled) {
    // To get position in WORLD reference
    for (auto &obj : objects.object_list) {
        sl::Translation pos = obj.position;
        sl::Translation new_pos = pos * current_camera_pose.getOrientation() + current_camera_pose.getTranslation();
        obj.position = sl::float3(new_pos.x, new_pos.y, new_pos.z);
    }

    // Initialize visualization
    if (!has_background_ready) {
        generateBackground();
    }

    tracking_view = background.clone();
    // Scale
    drawScale(tracking_view);
    
    //Jae
    //std::cout << "[Jae] generate_view - new_pos.x: " << new_pos.x << std::endl;

    if (tracking_enabled) {
        // First add new points, and remove the ones that are too old
        uint64_t current_timestamp = objects.timestamp.getNanoseconds();
        addToTracklets(objects);
        detectUnchangedTrack(current_timestamp);
        pruneOldPoints();

        // Draw all tracklets
        drawTracklets(tracking_view, current_camera_pose);
    } else {
        drawPosition(objects, tracking_view, current_camera_pose);
    }
}

void TrackingViewer::zoomIn() {
    const float zoom_factor = 0.9f;
    zoom(zoom_factor);
}

void TrackingViewer::zoomOut() {
    const float zoom_factor = 1.0f / 0.9f;
    zoom(zoom_factor);
}

// ----------- Private methods ----------------------

void TrackingViewer::addToTracklets(sl::Objects &objects) {
    uint64_t current_timestamp = objects.timestamp.getNanoseconds();
    for (const auto &obj : objects.object_list) {
        unsigned int id = obj.id;

        if (obj.tracking_state != sl::OBJECT_TRACKING_STATE::OK) {
            continue;
        }

        bool new_object = true;
        for (Tracklet &track : tracklets) {
            if (track.id == id && track.is_alive) {
                new_object = false;
                track.addDetectedPoint(obj, current_timestamp, smoothing_window_size);
            }
        }

        // In case this object does not belong to existing tracks
        if (new_object) {
            // printf(" - Adding new tracklet with id: %u\n", id);
            Tracklet new_track(obj, obj.label, current_timestamp);
            tracklets.push_back(new_track);
        }
    }
}

void TrackingViewer::detectUnchangedTrack(uint64_t current_timestamp) {
    for (size_t track_index = 0; track_index < tracklets.size(); ++track_index) {
        Tracklet &track = tracklets[track_index];
        if (track.last_detected_timestamp != current_timestamp) {
            // If track missed more than N frames, delete it
            if (current_timestamp - track.last_detected_timestamp >= (max_missing_points * frame_time_step)) {
                track.is_alive = false;
                continue;
            }
        }
    }
}

void TrackingViewer::pruneOldPoints() {
    std::vector<size_t> track_to_delete; // If a dead track does not contain drawing points, juste erase it
    for (size_t track_index = 0; track_index < tracklets.size(); ++track_index) {
        if (tracklets[track_index].is_alive) {
            while (tracklets[track_index].positions.size() > history_size) {
                tracklets[track_index].positions.pop_front();
            }
            while (tracklets[track_index].positions_to_draw.size() > history_size) {
                tracklets[track_index].positions_to_draw.pop_front();
            }
        } else { // Here, we fade the dead trajectories faster than the alive one (4 points every frame)
            for (size_t i = 0; i < 4; ++i) {
                if (tracklets[track_index].positions.size() > 0) {
                    tracklets[track_index].positions.pop_front();
                }
                if (tracklets[track_index].positions_to_draw.size() > 0) {
                    tracklets[track_index].positions_to_draw.pop_front();
                } else {
                    track_to_delete.push_back(track_index);
                    break;
                }
            }
        }
    }

    int size_ = static_cast<int>(track_to_delete.size() - 1);
    for (int i = size_; i >= 0; --i) 
        tracklets.erase(tracklets.begin() + track_to_delete[i]);
    
}

void TrackingViewer::computeFOV() {
    sl::Resolution image_size = camera_calibration.left_cam.image_size;
    float fx = camera_calibration.left_cam.fx;
    fov = 2.0f * atan(image_size.width / (2.0f * fx));
}

void TrackingViewer::zoom(const float factor) {
    x_min *= factor;
    x_max *= factor;
    z_min *= factor;

    // Recompute x_step and z_step
    x_step = (x_max - x_min) / window_width;
    z_step = abs(z_min) / (window_height - camera_offset);
}

// ------------------------------------------------------
//          Drawing functions
// ------------------------------------------------------

void TrackingViewer::drawTracklets(cv::Mat &tracking_view, sl::Pose current_camera_pose) {
    for (const Tracklet track : tracklets) {
        if (track.tracking_state != sl::OBJECT_TRACKING_STATE::OK) {
            continue;
        }
        if (int(track.positions_to_draw.size()) < min_length_to_draw) {
            continue;
        }

        auto clr = generateColorID_u((int)track.id);

        size_t track_size = track.positions_to_draw.size();
        TrackPoint start_point = track.positions_to_draw[0];
        cv::Point2i cv_start_point = toCVPoint(start_point, current_camera_pose);
        TrackPoint end_point = track.positions_to_draw[0];
        for (size_t point_index = 1; point_index < track_size; ++point_index) {
            end_point = track.positions_to_draw[point_index];
            cv::Point2i cv_end_point = toCVPoint(track.positions_to_draw[point_index], current_camera_pose);

            // Check point status
            if (start_point.tracking_state == TrackPointState::OFF || end_point.tracking_state == TrackPointState::OFF)
                continue;
            
            cv::line( tracking_view, cv_start_point, cv_end_point, clr, 4 );
            start_point = end_point;
            cv_start_point = cv_end_point;
        }

        // Current position, visualized as a point, only for alived track
        // Point = person || Square = Vehicle 
        if (track.is_alive) {
            switch (track.object_type) {
                case sl::OBJECT_CLASS::PERSON:
                    cv::circle(tracking_view, toCVPoint(track.positions_to_draw.back(), current_camera_pose), 5, clr, 5);
                    break;
                case sl::OBJECT_CLASS::VEHICLE:
                {
                    cv::Point2i rect_center = toCVPoint(track.positions_to_draw.back(), current_camera_pose);
                    int square_size = 10;
                    cv::Point2i top_left_corner = rect_center - cv::Point2i(square_size, square_size * 2);
                    cv::Point2i right_bottom_corner = rect_center + cv::Point2i(square_size, square_size * 2);
#if (defined(CV_VERSION_EPOCH) && CV_VERSION_EPOCH == 2)
                    cv::rectangle(tracking_view, top_left_corner, right_bottom_corner, clr, -1); //OpenCV 2.X --> cv::FILLED not defined but negative values
#else
                    cv::rectangle(tracking_view, top_left_corner, right_bottom_corner, clr, cv::FILLED);
#endif
                    break;
                }
                case sl::OBJECT_CLASS::LAST:
                    break;
                default:
                    break;
            }
        }
    }
}

void TrackingViewer::drawPosition(sl::Objects &objects, cv::Mat &tracking_view, sl::Pose current_camera_pose) {
    for (auto obj : objects.object_list) {
        sl::float4 generated_color_sl = getColorClass((int) obj.label)* 255.0f;
        cv::Scalar generated_color(generated_color_sl.x, generated_color_sl.y, generated_color_sl.z);

        // Point = person || Rect = Vehicle 
        switch (obj.label) {
            case sl::OBJECT_CLASS::PERSON:
                cv::circle(tracking_view, toCVPoint(obj.position, current_camera_pose), 5, generated_color, 5);
                break;
            case sl::OBJECT_CLASS::VEHICLE:
            {
                if (!obj.bounding_box.empty()) {
                    cv::Point2i rect_center = toCVPoint(obj.position, current_camera_pose);
                    int square_size = 10;
                    cv::Point2i top_left_corner = rect_center - cv::Point2i(square_size, square_size * 2);
                    cv::Point2i right_bottom_corner = rect_center + cv::Point2i(square_size, square_size * 2);
#if (defined(CV_VERSION_EPOCH) && CV_VERSION_EPOCH == 2)
                    cv::rectangle(tracking_view, top_left_corner, right_bottom_corner, generated_color, -1); //OpenCV 2.X --> cv::FILLED not defined but negative values
#else
                    cv::rectangle(tracking_view, top_left_corner, right_bottom_corner, generated_color, cv::FILLED);
#endif
                }
                break;
            }
            case sl::OBJECT_CLASS::LAST:
                break;
            default:
                break;
        }
    }
}

void TrackingViewer::drawScale(cv::Mat &tracking_view) {
    int one_meter_horizontal = static_cast<int>(1000.f / x_step + .5f);
    cv::Point2i st_pt(25, window_height - 50);
    cv::Point2i end_pt(25 + one_meter_horizontal, window_height - 50);
    int thickness = 1;

    // Scale line
    cv::line(tracking_view, st_pt, end_pt, cv::Scalar(0, 0, 0), thickness);

    // Add ticks
    cv::line(tracking_view, st_pt + cv::Point2i(0, -3), st_pt + cv::Point2i(0, 3), cv::Scalar(0, 0, 0), thickness);
    cv::line(tracking_view, end_pt + cv::Point2i(0, -3), end_pt + cv::Point2i(0, 3), cv::Scalar(0, 0, 0), thickness);

    // Scale text
    cv::putText(tracking_view, "1m", end_pt + cv::Point2i(5, 5), 1, 1.0, cv::Scalar(0, 0, 0), 1);
}

void TrackingViewer::generateBackground() {
    background = cv::Mat(window_height, window_width, CV_8UC3, background_color);

    // Draw camera + hotkeys information
    drawCamera();
    drawHotkeys();
}

void TrackingViewer::drawCamera() {
    // Configuration
    cv::Scalar camera_color(255, 117, 44);

    int camera_size = 10;
    int camera_height = window_height - camera_offset;
    cv::Point2i camera_left_pt(window_width / 2 - camera_size / 2, camera_height);
    cv::Point2i camera_right_pt(window_width / 2 + camera_size / 2, camera_height);

    // Drawing camera
    std::vector<cv::Point2i> camera_pts{
        cv::Point2i(window_width / 2 - camera_size, camera_height),
        cv::Point2i(window_width / 2 + camera_size, camera_height),
        cv::Point2i(window_width / 2 + camera_size, camera_height + camera_size / 2),
        cv::Point2i(window_width / 2 - camera_size, camera_height + camera_size / 2),};
    cv::fillConvexPoly(background, camera_pts, camera_color);

    // Compute the FOV
    if (fov < 0.0f) {
        computeFOV();
    }

    // Get FOV intersection with window borders
    float z_at_x_max = x_max / tan(fov / 2.0f);
    cv::Point2i left_intersection_pt = toCVPoint(x_min, -z_at_x_max), right_intersection_pt = toCVPoint(x_max, -z_at_x_max);

    uchar clr[3] = {static_cast<uchar>(camera_color(0)), static_cast<uchar>(camera_color(2)), static_cast<uchar>(camera_color(3))};
    // Draw FOV
    // Second try: dotted line
    cv::LineIterator left_line_it(background, camera_left_pt, left_intersection_pt, 8);
    for (int i = 0; i < left_line_it.count; ++i, ++left_line_it) {
        cv::Point2i current_pos = left_line_it.pos();
        if (i % 5 == 0 || i % 5 == 1) {
            (*left_line_it)[0] = clr[0];
            (*left_line_it)[1] = clr[1];
            (*left_line_it)[2] = clr[2];
        }

        for (int r = 0; r < current_pos.y; ++r) {
            float ratio = float(r) / camera_height;
            background.at<cv::Vec3b>(r, current_pos.x) = applyFading(background_color, ratio,  fov_color);
        }
    }

    cv::LineIterator right_line_it(background, camera_right_pt, right_intersection_pt, 8);
    for (int i = 0; i < right_line_it.count; ++i, ++right_line_it) {
        cv::Point2i current_pos = right_line_it.pos();
        if (i % 5 == 0 || i % 5 == 1) {
            (*right_line_it)[0] = clr[0];
            (*right_line_it)[1] = clr[1];
            (*right_line_it)[2] = clr[2];
        }

        for (int r = 0; r < current_pos.y; ++r) {
            float ratio = float(r) / camera_height;
            background.at<cv::Vec3b>(r, current_pos.x) = applyFading(background_color, ratio,  fov_color);
        }
    }

    for (int c = window_width / 2 - camera_size / 2; c <= window_width / 2 + camera_size / 2; ++c) {
        for (int r = 0; r < camera_height; ++r) {
            float ratio = float(r) / camera_height;
            background.at<cv::Vec3b>(r, c) = applyFading(background_color, ratio,  fov_color);
        }
    }
}

void TrackingViewer::drawHotkeys() {
    cv::Scalar hotkeys_clr(0, 0, 0);
    cv::putText( background, "Press 'i' to zoom in", cv::Point2i(25, window_height - 25), 1,
            1.0, hotkeys_clr, 1 );
    cv::putText( background, "Press 'o' to zoom out", cv::Point2i(25, window_height - 15), 1,
            1.0, hotkeys_clr, 1 );
}

// ------------------------------------------------------
//          UTILS: from 3D world to 2D track view
// ------------------------------------------------------
// Utils

cv::Point2i TrackingViewer::toCVPoint(double x, double z) {
    return cv::Point2i((x - x_min) / x_step, (z - z_min) / z_step);
}

// Utils with pose information

cv::Point2i TrackingViewer::toCVPoint(sl::float3 position, sl::Pose pose) {
    // Go to camera current pose
    sl::Rotation rotation = pose.getRotationMatrix();
    rotation.inverse();
    sl::Translation new_position = sl::Translation(position - pose.getTranslation()) * rotation.getOrientation();
    return cv::Point2i(static_cast<int>((new_position.tx - x_min) / x_step +.5f), static_cast<int>((new_position.tz - z_min) / z_step + .5f));
}

cv::Point2i TrackingViewer::toCVPoint(TrackPoint position, sl::Pose pose) {
    sl::Translation sl_position(position.toSLFloat());
    return toCVPoint(sl_position, pose);
}
