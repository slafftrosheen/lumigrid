# Timeline-Based Preset Sequencer (RTC-Enabled)

The Timeline Sequencer allows you to schedule preset (frame) playback based on time, date, or intervals with advanced calendar-based features. This feature enables automated display content rotation throughout the day, week, or specific events with support for date ranges, time windows, and exception dates.

## Features

- **Timeline Editor**: Web-based interface for creating and managing timelines
- **Continuous Playback**: Support for looping timelines that repeat automatically
- **RTC Integration**: Uses DS3231 Real-Time Clock for accurate, battery-backed scheduling
- **Calendar-Based Scheduling**: Advanced scheduling with date ranges, time windows, and exceptions
- **Multiple Trigger Types**:
  - **Time of Day**: Display preset at specific times daily (e.g., 8:00 AM every weekday)
  - **Specific Date/Time**: One-time or specific datetime triggers
  - **Interval**: Repeat presets at regular intervals (e.g., every 5 minutes)
  - **Manual**: Manually triggered presets only

## Advanced Calendar Features

### Date Ranges
Schedule entries to be active only during specific date ranges (e.g., seasonal displays):
- `valid_from`: Entry only triggers on or after this date (YYYY-MM-DD)
- `valid_until`: Entry only triggers on or before this date (YYYY-MM-DD)

### Time Windows
Restrict time-of-day triggers to specific time windows (e.g., business hours):
- `start_time`: Entry only triggers at or after this time (HH:MM:SS)
- `end_time`: Entry only triggers at or before this time (HH:MM:SS)
- Supports midnight-crossing ranges (e.g., 22:00:00 to 06:00:00)

### Exception Dates
Skip specific dates even if other conditions match (e.g., holidays):
- `exception_dates`: List of dates to skip (["YYYY-MM-DD", ...])

## Configuration

Add timelines to your `config.yaml`:

```yaml
timelines:
  # Basic daily schedule
  - id: "daily-schedule"
    name: "Daily Schedule"
    description: "Automated daily display schedule"
    enabled: true
    loop: true
    entries:
      - id: "morning-time"
        preset_name: "Current Time"
        trigger_type: "time_of_day"
        time_of_day: "06:00:00"
        days_of_week: [0, 1, 2, 3, 4, 5, 6]  # 0=Monday, 6=Sunday
        duration: 0  # 0 = until next entry
        enabled: true
      
      - id: "evening-temp"
        preset_name: "Temperature"
        trigger_type: "time_of_day"
        time_of_day: "18:00:00"
        days_of_week: [0, 1, 2, 3, 4, 5, 6]
        duration: 0
        enabled: true
  
  # Advanced schedule with calendar features
  - id: "seasonal-business-schedule"
    name: "Seasonal Business Schedule"
    description: "Summer business hours with holiday exceptions"
    enabled: true
    loop: true
    entries:
      # Summer season display (June-August only)
      - id: "summer-display"
        preset_name: "Summer Temperature"
        trigger_type: "time_of_day"
        time_of_day: "09:00:00"
        days_of_week: [0, 1, 2, 3, 4]  # Weekdays
        valid_from: "2024-06-01"
        valid_until: "2024-08-31"
        start_time: "08:00:00"  # Business hours start
        end_time: "17:00:00"    # Business hours end
        exception_dates:  # Skip holidays
          - "2024-07-04"  # Independence Day
        duration: 0
        enabled: true
```


## Using the Web Interface

### Accessing the Timeline Editor

1. Navigate to the web interface: `http://your-pi-ip:5000`
2. Click on **Timelines** in the sidebar navigation
3. You'll see the Timeline Sequencer page with:
   - List of existing timelines
   - Sequencer status (running/stopped, active timeline, etc.)
   - Button to create new timelines

### Creating a Timeline

1. Click **Create Timeline** button
2. Fill in the timeline details:
   - **Timeline Name**: Descriptive name for your timeline
   - **Description**: Optional description
   - **Enabled**: Check to activate the timeline immediately
   - **Loop Playback**: Check to repeat the timeline continuously

3. Add timeline entries:
   - Click **Add Entry** to add a new scheduled preset
   - For each entry:
     - **Preset Name**: Select from your available presets/frames
     - **Trigger Type**: Choose when this preset should play
     - **Time/Date**: Set the trigger time (format depends on trigger type)
     - **Days of Week**: For time-of-day triggers, select which days
     - **Duration**: How long to display (0 = until next entry)
     - **Enabled**: Enable/disable this entry

4. Click **Save Timeline** to create the timeline

### Editing a Timeline

1. Find the timeline in the list
2. Click **Edit** button
3. Modify the timeline settings and entries
4. Click **Save Timeline** to apply changes

### Activating/Deactivating Timelines

- Click **Activate** to enable a timeline (it will start scheduling presets)
- Click **Deactivate** to disable a timeline (stops scheduling)
- Only enabled timelines will trigger preset playback

### Deleting a Timeline

1. Click **Delete** button on the timeline
2. Confirm the deletion
3. The timeline will be permanently removed

## Trigger Types Explained

### Time of Day

Triggers at a specific time each day:

```yaml
trigger_type: "time_of_day"
time_of_day: "08:00:00"  # HH:MM:SS format
days_of_week: [0, 1, 2, 3, 4]  # Monday-Friday
```

- Use 24-hour time format
- Specify days of week (0=Monday, 6=Sunday)
- Triggers every selected day at the specified time

### Specific Date & Time

Triggers once at a specific date and time:

```yaml
trigger_type: "datetime"
datetime: "2024-12-25T09:00:00"  # ISO format
```

- Use ISO 8601 format: `YYYY-MM-DDTHH:MM:SS`
- Triggers only once at the specified datetime
- Useful for special events or one-time displays

### Interval

Triggers repeatedly at regular intervals:

```yaml
trigger_type: "interval"
interval_seconds: 300  # Every 5 minutes
```

- Specify interval in seconds
- Repeats continuously while timeline is enabled
- Good for rotating through multiple presets

### Manual

Manual trigger only (no automatic scheduling):

```yaml
trigger_type: "manual"
```

- Does not trigger automatically
- Can be triggered via API or other means
- Useful for presets that should only display on command

## API Endpoints

### Get All Timelines

```bash
GET /api/timelines
```

Returns list of all timelines.

### Create Timeline

```bash
POST /api/timelines
Content-Type: application/json

{
  "name": "My Timeline",
  "description": "Description",
  "enabled": true,
  "loop": true,
  "entries": [...]
}
```

### Get Timeline

```bash
GET /api/timelines/<timeline_id>
```

### Update Timeline

```bash
PUT /api/timelines/<timeline_id>
Content-Type: application/json

{
  "name": "Updated Name",
  ...
}
```

### Delete Timeline

```bash
DELETE /api/timelines/<timeline_id>
```

### Activate Timeline

```bash
POST /api/timelines/<timeline_id>/activate
```

### Deactivate Timeline

```bash
POST /api/timelines/<timeline_id>/deactivate
```

### Get Sequencer Status

```bash
GET /api/sequencer/status
```

Returns current sequencer status including active timeline and preset.

## Example Use Cases

### Office Display Schedule

Display different content throughout the workday:

```yaml
- name: "Office Hours"
  enabled: true
  loop: true
  entries:
    - preset_name: "Welcome Message"
      trigger_type: "time_of_day"
      time_of_day: "08:00:00"
      days_of_week: [0, 1, 2, 3, 4]  # Weekdays
    
    - preset_name: "Meeting Room Schedule"
      trigger_type: "time_of_day"
      time_of_day: "09:00:00"
      days_of_week: [0, 1, 2, 3, 4]
    
    - preset_name: "Lunch Break"
      trigger_type: "time_of_day"
      time_of_day: "12:00:00"
      days_of_week: [0, 1, 2, 3, 4]
    
    - preset_name: "End of Day"
      trigger_type: "time_of_day"
      time_of_day: "17:00:00"
      days_of_week: [0, 1, 2, 3, 4]
```

### Weather Station Rotation

Rotate through weather data displays:

```yaml
- name: "Weather Rotation"
  enabled: true
  loop: true
  entries:
    - preset_name: "Current Temperature"
      trigger_type: "interval"
      interval_seconds: 10
    
    - preset_name: "Current Time"
      trigger_type: "interval"
      interval_seconds: 10
    
    - preset_name: "Current Date"
      trigger_type: "interval"
      interval_seconds: 10
```

### Event Countdown

Display countdown for special events:

```yaml
- name: "Holiday Events"
  enabled: true
  loop: false
  entries:
    - preset_name: "New Year Countdown"
      trigger_type: "datetime"
      datetime: "2024-12-31T23:59:00"
    
    - preset_name: "New Year Message"
      trigger_type: "datetime"
      datetime: "2025-01-01T00:00:00"
```

### Seasonal Schedule with Date Ranges

Different content for different seasons:

```yaml
- name: "Seasonal Displays"
  enabled: true
  loop: true
  entries:
    # Spring display (March-May)
    - preset_name: "Spring Flowers"
      trigger_type: "time_of_day"
      time_of_day: "08:00:00"
      days_of_week: [0, 1, 2, 3, 4, 5, 6]
      valid_from: "2024-03-01"
      valid_until: "2024-05-31"
      duration: 0
    
    # Summer display (June-August)
    - preset_name: "Summer Sun"
      trigger_type: "time_of_day"
      time_of_day: "08:00:00"
      days_of_week: [0, 1, 2, 3, 4, 5, 6]
      valid_from: "2024-06-01"
      valid_until: "2024-08-31"
      duration: 0
    
    # Fall display (September-November)
    - preset_name: "Autumn Leaves"
      trigger_type: "time_of_day"
      time_of_day: "08:00:00"
      days_of_week: [0, 1, 2, 3, 4, 5, 6]
      valid_from: "2024-09-01"
      valid_until: "2024-11-30"
      duration: 0
    
    # Winter display (December-February)
    - preset_name: "Winter Snow"
      trigger_type: "time_of_day"
      time_of_day: "08:00:00"
      days_of_week: [0, 1, 2, 3, 4, 5, 6]
      valid_from: "2024-12-01"
      valid_until: "2025-02-28"
      duration: 0
```

### Business Hours with Time Windows

Restrict displays to specific hours:

```yaml
- name: "Business Hours Only"
  enabled: true
  loop: true
  entries:
    - preset_name: "Office Status"
      trigger_type: "time_of_day"
      time_of_day: "09:00:00"
      days_of_week: [0, 1, 2, 3, 4]  # Weekdays only
      start_time: "08:00:00"  # Only active from 8 AM
      end_time: "17:00:00"    # Until 5 PM
      duration: 0
```

### Holiday Exception Handling

Skip scheduled displays on holidays:

```yaml
- name: "Weekday Schedule with Holidays"
  enabled: true
  loop: true
  entries:
    - preset_name: "Daily Announcements"
      trigger_type: "time_of_day"
      time_of_day: "09:00:00"
      days_of_week: [0, 1, 2, 3, 4]  # Weekdays
      exception_dates:  # Skip these holidays
        - "2024-01-01"  # New Year's Day
        - "2024-07-04"  # Independence Day
        - "2024-12-25"  # Christmas
        - "2024-11-28"  # Thanksgiving
      duration: 0
```

## Best Practices

1. **Test Before Enabling**: Create timelines with `enabled: false` first, then activate after reviewing
2. **Use Descriptive Names**: Name your timelines and entries clearly
3. **Avoid Overlaps**: Be careful with overlapping time triggers
4. **Set Appropriate Durations**: Use `duration: 0` for most entries to display until the next trigger
5. **Monitor Active Timeline**: Check the sequencer status to see which timeline is currently active
6. **Use Date Ranges for Seasonal Content**: Take advantage of `valid_from` and `valid_until` for seasonal displays
7. **Leverage Exception Dates**: Use `exception_dates` to handle holidays and special occasions
8. **Configure Time Windows**: Use `start_time` and `end_time` to restrict displays to specific hours

## Troubleshooting

### Timeline Not Triggering

- Verify the timeline is **enabled**
- Check that entries are also **enabled**
- Ensure the system time is correct (especially if using RTC)
- Check that the trigger time format is correct
- Verify days_of_week includes the current day (0=Monday)
- Check if current date is outside `valid_from`/`valid_until` range
- Verify current time is within `start_time`/`end_time` window
- Check if today's date is in `exception_dates` list

### Presets Not Found

- Ensure the preset name exactly matches an existing frame/preset
- Check that frames are enabled in the content manager
- Verify preset exists in `/api/presets` endpoint

### Multiple Timelines Conflict

- Only one timeline entry can be active at a time
- The most recently triggered entry takes precedence
- Consider using separate timelines for different scheduling patterns

### Calendar Features Not Working

- Ensure RTC is properly configured and providing accurate time
- Verify date/time formats match requirements (YYYY-MM-DD, HH:MM:SS)
- Check logs for validation errors
- Confirm `valid_from` is before `valid_until`

## Technical Details

### Implementation

The sequencer runs in a background thread that:
1. Checks all enabled timelines every second
2. Uses RTC time when available for accurate scheduling
3. Evaluates trigger conditions for all entries
4. Applies calendar-based constraints (date ranges, time windows, exceptions)
3. Activates the preset when a trigger fires
4. Prevents duplicate triggers within 2-minute windows

### Time Precision

- Trigger checks occur every 1 second
- Time-based triggers have a 60-second window
- RTC (if available) is used for accurate timekeeping
- System time is used as fallback

### Performance

- Minimal CPU overhead (checks run once per second)
- No impact on display rendering
- Thread-safe timeline management
- Configuration persisted to `config.yaml`

## Integration with RTC

When RTC (Real-Time Clock) is enabled:

```yaml
rtc:
  enabled: true
  type: "DS3231"
  sync_interval: 3600
```

The sequencer uses RTC for accurate timekeeping, ensuring:
- Precise trigger timing even without network
- Consistent scheduling across reboots
- Battery-backed time when power is lost

## Future Enhancements

Potential future features:
- Sunrise/sunset triggers
- Weather-based conditional triggers
- Holiday calendar integration
- Timeline templates
- Timeline import/export
