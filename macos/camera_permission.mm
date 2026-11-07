// macOS camera permission helper.
// OpenCV's AVFoundation backend only fires an async permission request and fails
// immediately, so we ask for access up front and block until the user answers.
#import <AVFoundation/AVFoundation.h>
#include <cstdio>

bool RequestCameraAccess() {
    AVAuthorizationStatus status = [AVCaptureDevice authorizationStatusForMediaType:AVMediaTypeVideo];
    switch (status) {
        case AVAuthorizationStatusAuthorized:
            return true;
        case AVAuthorizationStatusNotDetermined: {
            __block BOOL granted = NO;
            dispatch_semaphore_t sem = dispatch_semaphore_create(0);
            [AVCaptureDevice requestAccessForMediaType:AVMediaTypeVideo
                                     completionHandler:^(BOOL ok) {
                granted = ok;
                dispatch_semaphore_signal(sem);
            }];
            dispatch_semaphore_wait(sem, DISPATCH_TIME_FOREVER);
            if (!granted) std::fprintf(stderr, "Camera access was denied.\n");
            return granted;
        }
        default:
            std::fprintf(stderr,
                "Camera access is denied or restricted. Enable it in "
                "System Settings > Privacy & Security > Camera.\n");
            return false;
    }
}
