#include "project.h"
#include <stdio.h>
#include <stdlib.h>

#define REACTION_LIMIT_MS    1000u
#define ROUND_TIMEOUT_MS     3000u
#define PRE_ROUND_DELAY_MS    500u
#define POST_ROUND_DELAY_MS   500u
#define RELEASE_TIMEOUT_MS   2000u
#define IDLE_TIMEOUT_MS     10000u
#define BLINK_MS              200u
#define BUTTON_DEBOUNCE_MS    200u
#define LP_SCAN_PERIOD_MS     100u
#define WDT_MATCH           32768u 

static volatile uint32_t msTicks = 0;
static volatile uint8_t  buttonEvent = 0;
static volatile uint32_t lastButtonIsrTime = 0;

static void SysTickHandler(void)
{
    msTicks++;
}

static inline uint32_t Millis(void)
{
    return msTicks;
}

static inline uint32_t Elapsed(uint32_t since)
{
    return Millis() - since;
}

CY_ISR(UserButtonISR)
{
    uint32_t now = Millis();
    if ((now - lastButtonIsrTime) >= BUTTON_DEBOUNCE_MS)
    {
        lastButtonIsrTime = now;
        buttonEvent = 1;
    }
    User_button_pin_ClearInterrupt();
}

// Watchdog
#define WDT_PERIOD_TICKS   40000u
#define WDT_FEED_EVERY_MS  100u

static uint32_t lastWdtFeed = 0;

static void WatchdogStart(void)
{
    CySysClkIloStart();
    CySysWdtSetMatch((CySysWdtGetCount() + WDT_PERIOD_TICKS) & 0xFFFFu);
    CySysWdtEnable();
    lastWdtFeed = Millis();
}

static inline void WatchdogFeed(void)
{
    if (Elapsed(lastWdtFeed) >= WDT_FEED_EVERY_MS)
    {
        lastWdtFeed = Millis();
        CySysWdtSetMatch((CySysWdtGetCount() + WDT_PERIOD_TICKS) & 0xFFFFu);
    }
}

static uint8_t  blinkActive = 0;
static uint32_t blinkStart  = 0;

static void TurnOffAllLEDs(void)
{
    LED_CapSense_Button_0_Write(1);
    LED_CapSense_Button_1_Write(1);
    LED_CapSense_Button_2_Write(1);
}

static void BlinkStart(void)
{
    LED_User_status_Write(0);
    blinkStart  = Millis();
    blinkActive = 1;
}

static void BlinkService(void)
{
    if (blinkActive && Elapsed(blinkStart) >= BLINK_MS)
    {
        LED_User_status_Write(1);
        blinkActive = 0;
    }
}

static uint8_t scanPending = 0;

static uint8_t CapSenseFinish(void)   // returns 1 when new data is ready
{
    if (scanPending && !CapSense_IsBusy())
    {
        CapSense_ProcessAllWidgets();
        scanPending = 0;
        return 1;
    }
    return 0;
}

static void CapSenseStartScan(void)
{
    if (!scanPending && !CapSense_IsBusy())
    {
        CapSense_ScanAllWidgets();
        scanPending = 1;
    }
}

static uint8_t ReadTouchMask(void)
{
    uint8_t m = 0;
    if (CapSense_IsWidgetActive(CapSense_BUTTON0_WDGT_ID)) m |= 0x01;
    if (CapSense_IsWidgetActive(CapSense_BUTTON1_WDGT_ID)) m |= 0x02;
    if (CapSense_IsWidgetActive(CapSense_BUTTON2_WDGT_ID)) m |= 0x04;
    return m;
}


// stat
static uint32_t roundCount = 0;
static uint32_t hitCount = 0;
static uint32_t totalReactionTime = 0;

static void OutputStats(void)
{
    char msg[80];
    uint32_t avg = (hitCount > 0) ? (totalReactionTime / hitCount) : 0;
    sprintf(msg, "Rounds: %lu, Hits: %lu, Avg Time: %lu ms\r\n",
            (unsigned long)roundCount, (unsigned long)hitCount,
            (unsigned long)avg);
    UART_UartPutString(msg);
}

static void MissFeedback(void)
{
    BlinkStart();
    OutputStats();
}

// low power mode
static void EnterLowPowerMode(void)
{
    uint32_t lastScan = Millis();

    TurnOffAllLEDs();
    LED_User_status_Write(1);
    blinkActive = 0;
    UART_UartPutString("Entering Low Power Mode...\r\n");
    buttonEvent = 0;

    for (;;)
    {
        WatchdogFeed();

        if (buttonEvent)
        {
            buttonEvent = 0;
            break;
        }

        if (CapSenseFinish() && CapSense_IsAnyWidgetActive())
        {
            break;
        }

        if (Elapsed(lastScan) >= LP_SCAN_PERIOD_MS)
        {
            lastScan = Millis();
            CapSenseStartScan();
        }

        CySysPmSleep();   // woken by SysTick (1 ms), button or CapSense ISR
    }

    UART_UartPutString("Woke from Low Power Mode\r\n");
}

// state machine
typedef enum
{
    ST_WAIT_START,     // waiting for User button
    ST_WAIT_RELEASE,   // wait until no CapSense is touched
    ST_PRE_ROUND,      // 500 ms pause
    ST_WAIT_TOUCH,     // LED on, waiting for the press
    ST_POST_ROUND      // 500 ms pause
} GameState;

static GameState state = ST_WAIT_START;
static uint32_t  stateStart = 0;
static uint32_t  lastActivity = 0;
static uint8_t   target = 0;
static uint8_t   repeatTarget = 0; 
static uint8_t   missReported = 0;

static void SetState(GameState s)
{
    state = s;
    stateStart = Millis();
}

static void StartGame(void)
{
    roundCount = 0;
    repeatTarget = 0;
    hitCount = 0;
    totalReactionTime = 0;
    TurnOffAllLEDs();
    srand(Millis() ^ (uint32_t)CySysTickGetValue());  // seed from button-press time
    UART_UartPutString("Game Started\r\n");
    SetState(ST_WAIT_RELEASE);
}

int main(void)
{
    uint8_t touchMask = 0;

    CyGlobalIntEnable;

    UART_Start();

    CySysTickStart();                       // default period = 1 ms
    CySysTickSetCallback(0, SysTickHandler);

    CapSense_Start();
    CapSenseStartScan();

    User_button_isr_StartEx(UserButtonISR);

    TurnOffAllLEDs();
    LED_User_status_Write(1);
    UART_UartPutString("Game Ready. Press User Button (SW2) to Start.\r\n");

    lastActivity = Millis();
    WatchdogStart();
    
    for (;;)
    {
        WatchdogFeed();
        BlinkService();

        // capsense 
        uint8_t fresh = CapSenseFinish();
        if (fresh)
        {
            touchMask = ReadTouchMask();
            if (touchMask) lastActivity = Millis();
            CapSenseStartScan();
        }
        else
        {
            CapSenseStartScan();
        }

        // user button
        if (buttonEvent)
        {
            buttonEvent = 0;
            lastActivity = Millis();
            if (state != ST_WAIT_START)
            {
                UART_UartPutString("Game Restarted by Button\r\n");
            }
            StartGame();
            continue;
        }

        // idle to low pwoer
        if (Elapsed(lastActivity) > IDLE_TIMEOUT_MS)
        {
            EnterLowPowerMode();
            touchMask = 0;
            lastActivity = Millis();
            SetState(ST_WAIT_START);
            UART_UartPutString("Press User Button (SW2) to Start.\r\n");
            continue;
        }

        // game logic
        switch (state)
        {
        case ST_WAIT_START:
            break;

        case ST_WAIT_RELEASE:
            if (fresh)
            {
                if (touchMask == 0)
                {
                    SetState(ST_PRE_ROUND);
                }
                else if (Elapsed(stateStart) > RELEASE_TIMEOUT_MS)
                {
                    UART_UartPutString("Buttons held too long, waiting...\r\n");
                    BlinkStart();
                    SetState(ST_WAIT_RELEASE);      // restart the wait
                }
            }
            break;

        case ST_PRE_ROUND:
            if (fresh && touchMask != 0)            // touched during pause
            {
                SetState(ST_WAIT_RELEASE);
            }
                else if (Elapsed(stateStart) >= PRE_ROUND_DELAY_MS)
                {
                    if (!repeatTarget)
                    {
                        target = (uint8_t)(rand() % 3);   // new led only for new roudn`
                    }
                    repeatTarget = 0;

                    switch (target)
                    {
                        case 0: LED_CapSense_Button_0_Write(0); break;
                        case 1: LED_CapSense_Button_1_Write(0); break;
                        default: LED_CapSense_Button_2_Write(0); break;
                    }
                    missReported = 0;
                    SetState(ST_WAIT_TOUCH);            // stateStart = LED-on time
                }
            break;

        case ST_WAIT_TOUCH:
            if (fresh && touchMask != 0)
            {
                uint32_t rt = Elapsed(stateStart);
                uint8_t pressed = (touchMask & 0x01) ? 0 :
                                  (touchMask & 0x02) ? 1 : 2;
                char msg[64];
                sprintf(msg, "Pressed: %u, Expected: %u, Time: %lu ms\r\n",
                        (unsigned)pressed, (unsigned)target, (unsigned long)rt);
                UART_UartPutString(msg);

                if (!missReported)
                {
                    roundCount++;
                    if (pressed == target && rt <= REACTION_LIMIT_MS)
                    {
                        hitCount++;
                        totalReactionTime += rt;
                    }
                    else
                    {
                        UART_UartPutString("Incorrect or Late Press!\r\n");
                        MissFeedback();
                    }
                }
                TurnOffAllLEDs();
                SetState(ST_POST_ROUND);            // next round, new random LED
            }
            else
            {
                uint32_t t = Elapsed(stateStart);

                if (!missReported && t > REACTION_LIMIT_MS)
                {
                    // 1 s passed with no touch - miss, LED stays on
                    missReported = 1;
                    // roundCount++;
                    UART_UartPutString("Timeout (no touch within 1 s)!\r\n");
                    MissFeedback();
                }

                if (t >= ROUND_TIMEOUT_MS)
                {
                    // 3 s passed with no touch - repeat the same round
                    UART_UartPutString("No touch for 3 s, repeating round...\r\n");
                    TurnOffAllLEDs();
                    repeatTarget = 1;
                    SetState(ST_POST_ROUND);
                }
            }
            break;

        case ST_POST_ROUND:
            if (Elapsed(stateStart) >= POST_ROUND_DELAY_MS)
            {
                SetState(ST_WAIT_RELEASE);
            }
            break;
        }
    }
}