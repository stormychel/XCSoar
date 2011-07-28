/*
Copyright_License {

  XCSoar Glide Computer - http://www.xcsoar.org/
  Copyright (C) 2000-2011 The XCSoar Project
  A detailed list of copyright holders can be found in the file "AUTHORS".

  This program is free software; you can redistribute it and/or
  modify it under the terms of the GNU General Public License
  as published by the Free Software Foundation; either version 2
  of the License, or (at your option) any later version.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with this program; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
}
*/

#include "Dialogs/Task.hpp"
#include "Dialogs/Internal.hpp"
#include "Dialogs/Waypoint.hpp"
#include "Screen/Layout.hpp"
#include "Screen/Fonts.hpp"
#include "Components.hpp"
#include "Dialogs/dlgTaskHelpers.hpp"
#include "DataField/Float.hpp"

#include "Task/TaskPoints/StartPoint.hpp"
#include "Task/TaskPoints/AATPoint.hpp"
#include "Task/TaskPoints/ASTPoint.hpp"
#include "Task/TaskPoints/FinishPoint.hpp"
#include "Task/ObservationZones/LineSectorZone.hpp"
#include "Task/ObservationZones/CylinderZone.hpp"
#include "Task/ObservationZones/AnnularSectorZone.hpp"
#include "Task/Visitors/TaskVisitor.hpp"
#include "Task/Visitors/TaskPointVisitor.hpp"
#include "Gauge/TaskView.hpp"
#include "Task/Visitors/ObservationZoneVisitor.hpp"
#include "Compiler.h"
#include "Look/Look.hpp"
#include "MainWindow.hpp"

#include <assert.h>
#include <stdio.h>

static WndForm *wf = NULL;
static WndFrame* wTaskView = NULL;
static OrderedTask* ordered_task = NULL;
static bool task_modified = false;
static unsigned active_index = 0;
static int next_previous = 0;

// setting to True during refresh so control values don't trigger form save
static bool Refreshing = false;

static void
OnCloseClicked(gcc_unused WndButton &Sender)
{
  wf->SetModalResult(mrOK);
}


class TPLabelObservationZone:
  public ObservationZoneConstVisitor
{
public:
  void
  Visit(gcc_unused const FAISectorZone& oz)
  {
    hide_all();
  }
  void
  Visit(gcc_unused const KeyholeZone& oz)
  {
    hide_all();
  }

  void
  Visit(gcc_unused const BGAFixedCourseZone& oz)
  {
    hide_all();
  }

  void
  Visit(gcc_unused const BGAEnhancedOptionZone& oz)
  {
    hide_all();
  }

  void
  Visit(gcc_unused const BGAStartSectorZone& oz)
  {
    hide_all();
  }

  void
  Visit(const SectorZone& oz)
  {
    hide_all();
    ShowFormControl(*wf, _T("frmOZSector"), true);

    LoadFormProperty(*wf, _T("prpOZSectorRadius"),
                     ugDistance, oz.getRadius());
    LoadFormProperty(*wf, _T("prpOZSectorStartRadial"),
                     oz.getStartRadial().value_degrees());
    LoadFormProperty(*wf, _T("prpOZSectorFinishRadial"),
                     oz.getEndRadial().value_degrees());

    ShowFormControl(*wf, _T("prpOZSectorInnerRadius"), false);
  }

  void
  Visit(const AnnularSectorZone& oz)
  {
    Visit((const SectorZone&)oz);
    LoadFormProperty(*wf, _T("prpOZSectorInnerRadius"),
                     ugDistance, oz.getInnerRadius());

    ShowFormControl(*wf, _T("prpOZSectorInnerRadius"), true);
  }

  void
  Visit(const LineSectorZone& oz)
  {
    hide_all();
    ShowFormControl(*wf, _T("frmOZLine"), true);

    LoadFormProperty(*wf, _T("prpOZLineLength"),
                     ugDistance, oz.getLength());
  }

  void
  Visit(const CylinderZone& oz)
  {
    hide_all();
    ShowFormControl(*wf, _T("frmOZCylinder"), true);

    LoadFormProperty(*wf, _T("prpOZCylinderRadius"),
                     ugDistance, oz.getRadius());
  }

private:
  void
  hide_all()
  {
    ShowFormControl(*wf, _T("frmOZLine"), false);
    ShowFormControl(*wf, _T("frmOZSector"), false);
    ShowFormControl(*wf, _T("frmOZCylinder"), false);
  }
};

/**
 * Utility class to read observation zone parameters and update the dlgTaskPoint dialog
 * items
 */
class TPReadObservationZone:
  public ObservationZoneVisitor
{
public:
  void
  Visit(gcc_unused FAISectorZone& oz)
  {
  }
  void
  Visit(gcc_unused KeyholeZone& oz)
  {
  }
  void
  Visit(gcc_unused BGAFixedCourseZone& oz)
  {
  }
  void
  Visit(gcc_unused BGAEnhancedOptionZone& oz)
  {
  }
  void
  Visit(gcc_unused BGAStartSectorZone& oz)
  {
  }
  void
  Visit(SectorZone& oz)
  {
    fixed radius =
      Units::ToSysDistance(GetFormValueFixed(*wf, _T("prpOZSectorRadius")));
    if (fabs(radius - oz.getRadius()) > fixed(49)) {
      oz.setRadius(radius);
      task_modified = true;
    }

    fixed start_radial = GetFormValueFixed(*wf, _T("prpOZSectorStartRadial"));
    if (start_radial != oz.getStartRadial().value_degrees()) {
      oz.setStartRadial(Angle::degrees(start_radial));
      task_modified = true;
    }

    fixed finish_radial = GetFormValueFixed(*wf, _T("prpOZSectorFinishRadial"));
    if (finish_radial != oz.getEndRadial().value_degrees()) {
      oz.setEndRadial(Angle::degrees(finish_radial));
      task_modified = true;
    }
  }

  void
  Visit(AnnularSectorZone& oz)
  {
    Visit((SectorZone&)oz);

    fixed radius =
      Units::ToSysDistance(GetFormValueFixed(*wf, _T("prpOZSectorInnerRadius")));
    if (fabs(radius - oz.getInnerRadius()) > fixed(49)) {
      oz.setInnerRadius(radius);
      task_modified = true;
    }
  }

  void
  Visit(LineSectorZone& oz)
  {
    fixed line_length =
      Units::ToSysDistance(GetFormValueFixed(*wf, _T("prpOZLineLength")));
    if (fabs(line_length - oz.getLength()) > fixed(49)) {
      oz.setLength(line_length);
      task_modified = true;
    }
  }

  void
  Visit(CylinderZone& oz)
  {
    fixed radius =
      Units::ToSysDistance(GetFormValueFixed(*wf, _T("prpOZCylinderRadius")));
    if (fabs(radius - oz.getRadius()) > fixed(49)) {
      oz.setRadius(radius);
      task_modified = true;
    }
  }
};

/**
 * for FAI tasks, make the zone sizes disabled so the user can't alter them
 * @param enable
 */
static void
EnableSizeEdit(bool enable)
{
  SetFormControlEnabled(*wf, _T("prpOZLineLength"), enable);
  SetFormControlEnabled(*wf, _T("prpOZCylinderRadius"), enable);
}

static void
RefreshView()
{
  wTaskView->invalidate();

  OrderedTaskPoint* tp = ordered_task->get_tp(active_index);
  if (!tp)
    return;

  Refreshing = true; // tell onChange routines not to save form!

  TPLabelObservationZone ozv;
  ObservationZoneConstVisitor &visitor = ozv;
  visitor.Visit(*tp->get_oz());

  WndFrame* wfrm = NULL;

  wfrm = ((WndFrame*)wf->FindByName(_T("lblType")));
  if (wfrm)
    wfrm->SetCaption(OrderedTaskPointName(ordered_task->get_factory().getType(*tp)));

  SetFormControlEnabled(*wf, _T("butPrevious"), active_index > 0);
  SetFormControlEnabled(*wf, _T("butNext"),
                        active_index < (ordered_task->task_size() - 1));

  WndButton* wb;
  wb = (WndButton*)wf->FindByName(_T("cmdOptionalStarts"));
  assert(wb);
  wb->set_visible(active_index == 0);
  if (ordered_task->optional_start_points_size() == 0)
    wb->SetCaption(_("Enable Alternate Starts"));
  else {
    TCHAR tmp[50];
    _stprintf(tmp, _T("%s (%d)"), _("Edit Alternates"),
        ordered_task->optional_start_points_size());
    wb->SetCaption(tmp);
  }

  EnableSizeEdit(ordered_task->get_factory_type() != TaskBehaviour::FACTORY_FAI_GENERAL);

  TCHAR bufType[100];
  TCHAR bufNamePrefix[100];

  switch (tp->GetType()) {
  case TaskPoint::START:
    _tcscpy(bufType, _T("Start point"));
    _tcscpy(bufNamePrefix, _T("Start: "));
    break;

  case TaskPoint::AST:
    _tcscpy(bufType, _T("Task point"));
    _stprintf(bufNamePrefix, _T("%d: "), active_index);
    break;

  case TaskPoint::AAT:
    _tcscpy(bufType, _T("Assigned area point"));
    _stprintf(bufNamePrefix, _T("%d: "), active_index);
    break;

  case TaskPoint::FINISH:
    _tcscpy(bufType, _T("Finish point"));
    _tcscpy(bufNamePrefix, _T("Finish: "));
    break;

  default:
    assert(true);
    break;
  }

  wf->SetCaption(bufType);

  wfrm = ((WndFrame*)wf->FindByName(_T("lblLocation")));
  if (wfrm) {
    TCHAR buff[100];
    _stprintf(buff, _T("%s %s"), bufNamePrefix, tp->get_waypoint().Name.c_str());
    wfrm->SetCaption(buff);
  }

  Refreshing = false; // reactivate onChange routines
}

static void
ReadValues()
{
  TPReadObservationZone tpv;
  OrderedTaskPoint* tp = ordered_task->get_tp(active_index);
  ObservationZoneVisitor &visitor = tpv;
  visitor.Visit(*tp->get_oz());
}

static void
OnTaskPaint(WndOwnerDrawFrame *Sender, Canvas &canvas)
{
  PixelRect rc = Sender->get_client_rect();

  OrderedTaskPoint* tp = ordered_task->get_tp(active_index);

  if (!tp) {
    canvas.clear_white();
    return;
  }

  const Look &look = *CommonInterface::main_window.look;
  PaintTaskPoint(canvas, rc, *ordered_task, *tp,
                 XCSoarInterface::Basic().location,
                 XCSoarInterface::SettingsMap(),
                 look.task, look.airspace,
                 terrain);
}

static void 
OnRemoveClicked(gcc_unused WndButton &Sender)
{
  if (MessageBoxX(_("Remove task point?"),
                        _("Task Point"), MB_YESNO | MB_ICONQUESTION) != IDYES)
    return;

  if (!ordered_task->get_factory().remove(active_index))
    return;

  task_modified = true;
  wf->SetModalResult(mrCancel);
}

static void
OnDetailsClicked(gcc_unused WndButton &Sender)
{
  OrderedTaskPoint* task_point = ordered_task->get_tp(active_index);
  if (task_point)
    dlgWaypointDetailsShowModal(wf->GetMainWindow(), task_point->get_waypoint(), false);
}

static void
OnRelocateClicked(gcc_unused WndButton &Sender)
{
  const GeoPoint &gpBearing = (active_index ?
                               ordered_task->get_tp(active_index - 1)->get_location() :
                               XCSoarInterface::Basic().location);

  const Waypoint *wp = dlgWaypointSelect(wf->GetMainWindow(), gpBearing,
                                         ordered_task,
                                         active_index);
  if (wp == NULL)
    return;

  ordered_task->get_factory().relocate(active_index, *wp);
  task_modified = true;
  RefreshView();
}

static void
OnTypeClicked(gcc_unused WndButton &Sender)
{
  if (dlgTaskPointType(wf->GetMainWindow(), &ordered_task, active_index)) {
    task_modified = true;
    RefreshView();
  }
}

static void
OnPreviousClicked(gcc_unused WndButton &Sender)
{
  if (active_index > 0) {
    next_previous=-1;
    wf->SetModalResult(mrOK);
  }
}

static void
OnNextClicked(gcc_unused WndButton &Sender)
{
  if (active_index < (ordered_task->task_size() - 1)) {
    next_previous=1;
    wf->SetModalResult(mrOK);
  }
}

/**
 * displays dlgTaskOptionalStarts
 * @param Sender
 */
static void
OnOptionalStartsClicked(gcc_unused WndButton &Sender)
{
  if (dlgTaskOptionalStarts(wf->GetMainWindow(), &ordered_task)) {
    task_modified =true;
    RefreshView();
  }
}

static void
OnOZLineLengthData(gcc_unused DataField *Sender, gcc_unused DataField::DataAccessKind_t Mode)
{
  if (!Refreshing)
    ReadValues();
  wTaskView->invalidate();
}

static void
OnOZCylinderRadiusData(gcc_unused DataField *Sender, gcc_unused DataField::DataAccessKind_t Mode)
{
  if (!Refreshing)
    ReadValues();
  wTaskView->invalidate();
}

static void
OnOZSectorRadiusData(gcc_unused DataField *Sender, gcc_unused DataField::DataAccessKind_t Mode)
{
  if (!Refreshing)
    ReadValues();
  wTaskView->invalidate();
}

static void
OnOZSectorInnerRadiusData(gcc_unused DataField *Sender, gcc_unused DataField::DataAccessKind_t Mode)
{
  if (!Refreshing)
    ReadValues();
  wTaskView->invalidate();
}

static void
OnOZSectorStartRadialData(gcc_unused DataField *Sender, gcc_unused DataField::DataAccessKind_t Mode)
{
  if (!Refreshing)
    ReadValues();
  wTaskView->invalidate();
}

static void
OnOZSectorFinishRadialData(gcc_unused DataField *Sender, gcc_unused DataField::DataAccessKind_t Mode)
{
  if (!Refreshing)
    ReadValues();
  wTaskView->invalidate();
}

static CallBackTableEntry CallBackTable[] = {
  DeclareCallBackEntry(OnCloseClicked),
  DeclareCallBackEntry(OnRemoveClicked),
  DeclareCallBackEntry(OnRelocateClicked),
  DeclareCallBackEntry(OnDetailsClicked),
  DeclareCallBackEntry(OnTypeClicked),
  DeclareCallBackEntry(OnPreviousClicked),
  DeclareCallBackEntry(OnNextClicked),
  DeclareCallBackEntry(OnOptionalStartsClicked),
  DeclareCallBackEntry(OnTaskPaint),
  DeclareCallBackEntry(OnOZLineLengthData),
  DeclareCallBackEntry(OnOZCylinderRadiusData),
  DeclareCallBackEntry(OnOZSectorRadiusData),
  DeclareCallBackEntry(OnOZSectorInnerRadiusData),
  DeclareCallBackEntry(OnOZSectorStartRadialData),
  DeclareCallBackEntry(OnOZSectorFinishRadialData),
  DeclareCallBackEntry(NULL)
};

bool
dlgTaskPointShowModal(SingleWindow &parent, OrderedTask** task,
                      const unsigned index)
{
  ordered_task = *task;
  task_modified = false;
  active_index = index;

  wf = LoadDialog(CallBackTable, parent,
                  Layout::landscape ? _T("IDR_XML_TASKPOINT_L") :
                                      _T("IDR_XML_TASKPOINT"));
  assert(wf != NULL);

  wTaskView = (WndFrame*)wf->FindByName(_T("frmTaskView"));
  assert(wTaskView != NULL);

  WndFrame* wType = (WndFrame*) wf->FindByName(_T("lblType"));
  assert (wType);
  wType->SetFont(Fonts::MapBold);

  WndFrame* wLocation = (WndFrame*) wf->FindByName(_T("lblLocation"));
  assert (wLocation);
  wLocation->SetFont(Fonts::MapBold);

  do {
    RefreshView();
    next_previous=0;
    if (wf->ShowModal() == mrOK)
      ReadValues();
    active_index += next_previous;
  } while (next_previous != 0);

  delete wf;

  if (*task != ordered_task) {
    *task = ordered_task;
    task_modified = true;
  } 
  if (task_modified) {
    ordered_task->update_geometry();
  }
  return task_modified;
}
