/*++
      Copyright (c) Microsoft Corporation. All Rights Reserved.
      Sample code. Dealpoint ID #843729.

      Module Name:

            rmiinternal.c

      Abstract:

            Contains Synaptics initialization code

      Environment:

            Kernel mode

      Revision History:

--*/

#include <Cross Platform Shim\compat.h>
#include <report.h>
#include <ft5x\ftinternal.h>
#include <ft5x\ftfwupdate.h>
#include <ftinternal.tmh>
#include <_spb.h>

NTSTATUS
Ft5xBuildFunctionsTable(
      IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
      IN SPB_CONTEXT* SpbContext
)
{
      UNREFERENCED_PARAMETER(SpbContext);

      //
      // Open the shared named kernel event created by Wacom pen driver.
      // When signaled, pen is in proximity and we should suppress touch.
      //
      {
            UNICODE_STRING eventName;
            RtlInitUnicodeString(&eventName, L"\\BaseNamedObjects\\WacomPenActive");
            ControllerContext->PenActiveEvent = IoCreateNotificationEvent(
                  &eventName,
                  &ControllerContext->PenActiveEventHandle);
            if (ControllerContext->PenActiveEvent != NULL)
            {
                  KeClearEvent(ControllerContext->PenActiveEvent);
            }
      }

      return STATUS_SUCCESS;
}

NTSTATUS
Ft5xChangePage(
      IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
      IN SPB_CONTEXT* SpbContext,
      IN int DesiredPage
)
{
      UNREFERENCED_PARAMETER(SpbContext);
      UNREFERENCED_PARAMETER(ControllerContext);
      UNREFERENCED_PARAMETER(DesiredPage);

      return STATUS_SUCCESS;
}

NTSTATUS
Ft5xConfigureFunctions(
    IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext
)
{
    NTSTATUS status;
    FT5X_CONTROLLER_CONTEXT* controller;
    controller = (FT5X_CONTROLLER_CONTEXT*)ControllerContext;

    UINT8 IdCmd[2] = { FTS_CMD_START1, FTS_CMD_START2 };
    UINT8 ChipId[2] = { 0 };
    status = FTS_Write(SpbContext, IdCmd, 2);
    if (!NT_SUCCESS(status)) {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INTERRUPT,
            "Failed to start - 0x%08lX",
            status);
        goto exit;
    }

    IdCmd[0] = FTS_CMD_READ_ID;
    IdCmd[1] = 0x0;

    status = FTS_Read(SpbContext, IdCmd, ChipId, 2);
    if (!NT_SUCCESS(status)) {
        Trace(
            TRACE_LEVEL_ERROR,
            TRACE_INTERRUPT,
            "Failed to read ChipID - 0x%08lX",
            status);
        goto exit;
    }

    Trace(TRACE_LEVEL_INFORMATION, TRACE_INTERRUPT, "Chip ID: 0x%02x%02x", ChipId[0], ChipId[1]);
exit:
    return STATUS_SUCCESS;
}

NTSTATUS
Ft5xGetObjectStatusFromControllerF12(
      IN VOID* ControllerContext,
      IN SPB_CONTEXT* SpbContext,
      IN DETECTED_OBJECTS* Data
)
/*++

Routine Description:

      This routine reads raw touch messages from hardware. If there is
      no touch data available (if a non-touch interrupt fired), the
      function will not return success and no touch data was transferred.

Arguments:

      ControllerContext - Touch controller context
      SpbContext - A pointer to the current i2c context
      Data - A pointer to any returned F11 touch data

Return Value:

      NTSTATUS, where only success indicates data was returned

--*/
{
    NTSTATUS status = STATUS_SUCCESS;
    FT5X_CONTROLLER_CONTEXT* controller;
    controller = (FT5X_CONTROLLER_CONTEXT*)ControllerContext;

    UINT32 base = 0;

    UINT8 input_id = 0;
    UINT8 event_flag = 0;
    UINT8 point_num = 0;
    UINT8 point[91] = { 0x0 };
    point[0] = 0x1;

    status = FTS_Read(SpbContext, point, point + 1, 90);
    if (!NT_SUCCESS(status)) {
        Trace(TRACE_LEVEL_ERROR, TRACE_INTERRUPT, "failed to read finger status data %!STATUS!", status);
        goto exit;
    }

    point_num = point[FTS_TOUCH_POINT_NUM] & 0x0F;
    if (point_num > FTS_MAX_POINTS_SUPPORT) {
        Trace(TRACE_LEVEL_ERROR, TRACE_INTERRUPT, "invalid point_num(%d)", point_num);
        goto exit;
    }

    for (UINT8 i = 0; i < FTS_MAX_POINTS_SUPPORT; i++) {
        base = FTS_ONE_TCH_LEN * i;
        input_id = point[FTS_TOUCH_ID_POS + base] >> 4;
        if (input_id >= FTS_MAX_ID)
            break;

        event_flag = point[FTS_TOUCH_EVENT_POS + base] >> 6;

        if (event_flag == FTS_TOUCH_DOWN || event_flag == FTS_TOUCH_CONTACT) {
            Data->States[input_id] = OBJECT_STATE_FINGER_PRESENT_WITH_ACCURATE_POS;
            Data->Positions[input_id].X = ((point[FTS_TOUCH_X_H_POS + base] & 0x0F) << 8) + (point[FTS_TOUCH_X_L_POS + base] & 0xFF);
            Data->Positions[input_id].Y = ((point[FTS_TOUCH_Y_H_POS + base] & 0x0F) << 8) + (point[FTS_TOUCH_Y_L_POS + base] & 0xFF);
        }
        else if (event_flag == FTS_TOUCH_UP) {
            Data->States[input_id] = OBJECT_STATE_NOT_PRESENT;
        }
    }

    //
    // If firmware reports 0 active points, force all states to NOT_PRESENT
    // to prevent stale CONTACT events from being interpreted as new touches
    //
    if (point_num == 0) {
        for (UINT8 i = 0; i < FTS_MAX_POINTS_SUPPORT; i++) {
            Data->States[i] = OBJECT_STATE_NOT_PRESENT;
        }
    }

exit:
    return status;
}

NTSTATUS
TchServiceObjectInterrupts(
      IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
      IN SPB_CONTEXT* SpbContext,
      IN PREPORT_CONTEXT ReportContext
)
{
      NTSTATUS status = STATUS_SUCCESS;
      DETECTED_OBJECTS data;

      RtlZeroMemory(&data, sizeof(data));

      //
      // If pen is in proximity, suppress all touch input (palm rejection).
      // The Wacom pen driver signals this via a shared kernel event.
      //
      if (ControllerContext->PenActiveEvent != NULL &&
          KeReadStateEvent(ControllerContext->PenActiveEvent) != 0)
      {
            //
            // Still read the hardware to clear the interrupt, but report
            // zero touches so the OS sees all fingers lifted.
            //
            status = Ft5xGetObjectStatusFromControllerF12(
                  ControllerContext,
                  SpbContext,
                  &data
            );

            //
            // Force all-up report to release any fingers currently down
            //
            RtlZeroMemory(&data, sizeof(data));
            status = ReportObjects(ReportContext, data);
            goto exit;
      }

      //
      // See if new touch data is available
      //
      status = Ft5xGetObjectStatusFromControllerF12(
            ControllerContext,
            SpbContext,
            &data
      );

      if (!NT_SUCCESS(status))
      {
            Trace(
                  TRACE_LEVEL_VERBOSE,
                  TRACE_SAMPLES,
                  "No object data to report - 0x%08lX",
                  status);

            goto exit;
      }

      status = ReportObjects(
            ReportContext,
            data);

      if (!NT_SUCCESS(status))
      {
            Trace(
                  TRACE_LEVEL_VERBOSE,
                  TRACE_SAMPLES,
                  "Error while reporting objects - 0x%08lX",
                  status);

            goto exit;
      }

exit:
      return status;
}


NTSTATUS
Ft5xServiceInterrupts(
      IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
      IN SPB_CONTEXT* SpbContext,
      IN PREPORT_CONTEXT ReportContext
)
{
      NTSTATUS status = STATUS_SUCCESS;

      TchServiceObjectInterrupts(ControllerContext, SpbContext, ReportContext);

      return status;
}

NTSTATUS
Ft5xSetReportingFlagsF12(
    IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext,
    IN UCHAR NewMode,
    OUT UCHAR* OldMode
)
{
      UNREFERENCED_PARAMETER(SpbContext);
      UNREFERENCED_PARAMETER(ControllerContext);
      UNREFERENCED_PARAMETER(NewMode);
      UNREFERENCED_PARAMETER(OldMode);

      return STATUS_SUCCESS;
}

NTSTATUS
Ft5xChangeChargerConnectedState(
    IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext,
    IN UCHAR ChargerConnectedState
)
{
      UNREFERENCED_PARAMETER(SpbContext);
      UNREFERENCED_PARAMETER(ControllerContext);
      UNREFERENCED_PARAMETER(ChargerConnectedState);

      return STATUS_SUCCESS;
}

NTSTATUS
Ft5xChangeSleepState(
    IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext,
    IN UCHAR SleepState
)
{
      UNREFERENCED_PARAMETER(SpbContext);
      UNREFERENCED_PARAMETER(ControllerContext);
      UNREFERENCED_PARAMETER(SleepState);

      return STATUS_SUCCESS;
}

NTSTATUS
Ft5xGetFirmwareVersion(
    IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext
)
{
      UNREFERENCED_PARAMETER(SpbContext);
      UNREFERENCED_PARAMETER(ControllerContext);

      return STATUS_SUCCESS;
}

NTSTATUS
Ft5xCheckInterrupts(
    IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext,
    IN ULONG* InterruptStatus
)
{
      UNREFERENCED_PARAMETER(SpbContext);
      UNREFERENCED_PARAMETER(ControllerContext);
      UNREFERENCED_PARAMETER(InterruptStatus);

      return STATUS_SUCCESS;
}

NTSTATUS
Ft5xConfigureInterruptEnable(
    IN FT5X_CONTROLLER_CONTEXT* ControllerContext,
    IN SPB_CONTEXT* SpbContext
)
{
      UNREFERENCED_PARAMETER(SpbContext);
      UNREFERENCED_PARAMETER(ControllerContext);

      return STATUS_SUCCESS;
}