////////////////////////////////////////////////////////////////////////////////
//    Copyright (c) Open Connectivity Foundation (OCF), AllJoyn Open Source
//    Project (AJOSP) Contributors and others.
//
//    SPDX-License-Identifier: Apache-2.0
//
//    All rights reserved. This program and the accompanying materials are
//    made available under the terms of the Apache License, Version 2.0
//    which accompanies this distribution, and is available at
//    http://www.apache.org/licenses/LICENSE-2.0
//
//    Copyright (c) Open Connectivity Foundation and Contributors to AllSeen
//    Alliance. All rights reserved.
//
//    Permission to use, copy, modify, and/or distribute this software for
//    any purpose with or without fee is hereby granted, provided that the
//    above copyright notice and this permission notice appear in all
//    copies.
//
//    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
//    WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
//    WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
//    AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
//    DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
//    PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
//    TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
//    PERFORMANCE OF THIS SOFTWARE.
////////////////////////////////////////////////////////////////////////////////

#import <Foundation/Foundation.h>
#import "AJNSessionOptions.h"
#import "AJNMessageArgument.h"

/**
 * Protocol implemented by AllJoyn apps and called by AllJoyn to fetch
 * the about data provided by the user
 */
@protocol AJNAboutDataListener <NSObject>

@required

/**
* Create the MsgArg that is returned when a user calls
* org.alljoyn.About.GetAboutData. The returned MsgArg must contain the
* AboutData dictionary for the Language specified.
*
* The MsgArg will contain the signature `a{sv}`.
*
* <table>
* <tr>
*     <th>Field Name</th>
*     <th>Required</th>
*     <th>Announced</th>
*     <th>Localized</th>
*     <th>Data type</th>
*     <th>Description</th>
* </tr>
* <tr>
*     <td><strong>AppId</strong></td>
*     <td>yes</td>
*     <td>yes</td>
*     <td>no</td>
*     <td>ay</td>
*     <td>The globally unique id for the application.</td>
* </tr>
* <tr>
*     <td><strong>DefaultLanguage</strong></td>
*     <td>yes</td>
*     <td>yes</td>
*     <td>no</td>
*     <td>s</td>
*     <td>The default language supported by the device. IETF langauge tags
*         specified by RFC 5646.</td>
* </tr>
* <tr>
*     <td><strong>DeviceName</strong></td>
*     <td>no</td>
*     <td>yes</td>
*     <td>yes</td>
*     <td>s</td>
*     <td>If Config service exist on the device, About instance populates
*         the value as DeviceName in Config; If there is not Config, it can
*         be set by the app.  Device Name is optional for a third party
*         apps but required for system apps. Versions of AllJoyn older than
*         14.12 this field was required.  If working with older applications
*         this field may be required.</td>
* </tr>
* <tr>
*     <td><strong>DeviceId</strong></td>
*     <td>yes</td>
*     <td>yes</td>
*     <td>no</td>
*     <td>s</td>
*     <td>A string with value generated using platform specific means.</td>
* </tr>
* <tr>
*     <td><strong>AppName</strong></td>
*     <td>yes</td>
*     <td>yes</td>
*     <td>yes</td>
*     <td>s</td>
*     <td>The application name assigned by the app manufacturer</td>
* </tr>
* <tr>
*     <td><strong>Manufacturer</strong></td>
*     <td>yes</td>
*     <td>yes</td>
*     <td>yes</td>
*     <td>s</td>
*     <td>The manufacturer's name.</td>
* </tr>
* <tr>
*     <td><strong>ModelNumber</strong></td>
*     <td>yes</td>
*     <td>yes</td>
*     <td>no</td>
*     <td>s</td>
*     <td>The app model number</td>
* </tr>
* <tr>
*     <td><strong>SupportedLanguages</strong></td>
*     <td>yes</td>
*     <td>no</td>
*     <td>no</td>
*     <td>as</td>
*     <td>A list of supported languages by the application</td>
* </tr>
* <tr>
*     <td><strong>Description</strong></td>
*     <td>yes</td>
*     <td>no</td>
*     <td>yes</td>
*     <td>s</td>
*     <td>Detailed description provided by the application.</td>
* </tr>
* <tr>
*     <td><strong>DateOfManufacture</strong></td>
*     <td>no</td>
*     <td>no</td>
*     <td>no</td>
*     <td>s</td>
*     <td>The date of manufacture. using format YYYY-MM-DD.
*         (Known as XML DateTime Format)</td>
* </tr>
* <tr>
*     <td><strong>SoftwareVersion</strong></td>
*     <td>yes</td>
*     <td>no</td>
*     <td>no</td>
*     <td>s</td>
*     <td>The software version of the manufacturer's software</td>
* </tr>
* <tr>
*     <td><strong>SoftwareVersion</strong></td>
*     <td>yes</td>
*     <td>no</td>
*     <td>no</td>
*     <td>s</td>
*     <td>The current version of the AllJoyn SDK utilized by the
*         application.</td>
* </tr>
* <tr>
*     <td><strong>HardwareVersion</strong></td>
*     <td>no</td>
*     <td>no</td>
*     <td>no</td>
*     <td>s</td>
*     <td>The device hardware version.</td>
* </tr>
* <tr>
*     <td><strong>SupportUrl</strong></td>
*     <td>no</td>
*     <td>no</td>
*     <td>no</td>
*     <td>s</td>
*     <td>The support URL.</td>
* </tr>
* </table>
*
* Custom fields are allowed. Since the proxy object only receives the field
* name and the MsgArg containing the contents for that field the default
* assumption is that user defined fields are:
* - are not required
* - are not announced
* - are localized if the MsgArg contains a String (not localized otherwise)
*
* Important: All implementations of GetAboutData should handle language
* specified as an empty string or NULL. In the case that the language is
* an empty string or NULL the GetAboutData is expected to return the
* default language.
*
* If the language tag given is not supported, return the best matching
* language according to RFC 4647 section 3.4. This algorithm requires
* that the "supported" languages be the least specific they can (e.g.,
                                                                 * "en" in order to match both "en" and "en-US" if requested), and the
* "requested" language be the most specific it can (e.g., "en-US" in
                                                    * order to match either "en-US" or "en" if supported).
*
* If the user has not provided ALL of the required fields return the QStatus
* #ER_ABOUT_ABOUTDATA_MISSING_REQUIRED_FIELD
*
* @param[out] msgArg a dictionary containing all of the AboutData fields for
*                    the requested language.  If language is not specified, the default
*                    language will be returned.
* @param[in] language IETF language tag specified by RFC 5646. If the string
*                     is NULL or an empty string, the MsgArg for the default
*                     language will be returned.
*
* @return
*  - ER_OK on successful
*  - ER_ABOUT_ABOUTDATA_MISSING_REQUIRED_FIELD if a required field is missing
*  - other error indicating failure
*/

- (QStatus)getAboutData:(AJNMessageArgument**)msgArg withLanguage:(NSString*)language;


/**
 * Return a MsgArg pointer containing dictionary containing the Announce
 * portion of the AboutData.
 *
 * The Announced values will always use the default language and will only
 * contain the fields that are announced.
 *
 * The fields required to be part of the announced MsgArg are:
 *  - AppId
 *  - DefaultLanguage
 *  - DeviceName (Optional since v14.12)
 *  - DeviceId
 *  - AppName
 *  - Manufacture
 *  - ModelNumber
 *
 * To read other fields or get the localized value of a field use the
 * org.alljoyn.About.GetAboutData method. This method is available using the
 * AboutProxy class.
 *
 * @param[out] msgArg a MsgArg dictionary with the a{sv} that contains the Announce
 *                    data.
 * @return ER_OK if successful
 */

- (QStatus)getAnnouncedAboutData:(AJNMessageArgument**)msgArg;


@end
