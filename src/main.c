/*
 * main.c — FreeRTOS bateria de testes completa para emulador RISC-V bare-metal
 *
 * Cada teste é identificado por um prefixo no log. Se rodar igual ao QEMU,
 * o emulador cobre praticamente 100% do que o FreeRTOS usa.
 *
 * Testes cobertos:
 *
 *  [T01] Preempção por prioridade
 *        Duas tasks em loop busy — a de maior prioridade deve dominar a CPU.
 *        Confirma que o scheduler preempta corretamente.
 *
 *  [T02] vTaskDelay — bloqueio por ticks
 *        Task acorda exatamente após N ticks. Testa o caminho tick→unblock.
 *
 *  [T03] vTaskDelayUntil — período fixo real
 *        Diferente do vTaskDelay, o período não deriva mesmo que a task
 *        demore a rodar. Testa o timer de lista de delayed tasks.
 *
 *  [T04] Semáforo binário — ping/pong entre tasks
 *        Sincronização clássica: PING give, PONG take. Testa bloqueio e
 *        desbloqueio por semáforo, e a entrega correta de contexto.
 *
 *  [T05] Semáforo de contagem
 *        Produtor dá N tokens, consumidor drena um por vez.
 *        Testa uxSemaphoreGetCount e o contador interno.
 *
 *  [T06] Mutex + exclusão mútua
 *        Três tasks competem pelo mesmo mutex. Apenas uma entra por vez.
 *        Testa xSemaphoreTake/Give com tipo mutex e a fila de prioridade.
 *
 *  [T07] Priority Inheritance (anti-inversão de prioridade)
 *        LOW toma mutex, HIGH tenta tomar → LOW deve herdar prioridade de HIGH
 *        temporariamente. Testa configUSE_MUTEXES com herança.
 *
 *  [T08] Queue — producer/consumer com dados
 *        PROD envia uint32_t, CONS lê e verifica sequência.
 *        Testa xQueueSend, xQueueReceive, bloqueio em fila cheia/vazia.
 *
 *  [T09] Queue — timeout (xQueueReceive com espera finita)
 *        Consumer espera X ticks. Se não chegar nada, loga TIMEOUT.
 *        Testa o caminho de timeout das queues.
 *
 *  [T10] vTaskSuspend / vTaskResume
 *        CTRL suspende TARGET, espera, e resume. TARGET conta quantas vezes
 *        rodou — deve parar durante a suspensão e retomar depois.
 *
 *  [T11] vTaskPrioritySet / uxTaskPriorityGet
 *        Task começa em prio 1, sobe para 3, desce de volta.
 *        Testa reordenação da ready list em tempo real.
 *
 *  [T12] vTaskDelete — task que se deleta
 *        Roda exatamente uma vez e chama vTaskDelete(NULL).
 *        Nunca deve aparecer uma segunda vez no log.
 *
 *  [T13] Software timers — one-shot
 *        Timer dispara uma única vez após 300ms.
 *        Testa xTimerCreate, xTimerStart e o callback.
 *
 *  [T14] Software timers — auto-reload (periódico)
 *        Timer dispara a cada 400ms indefinidamente.
 *        Testa o reload automático e xTimerStop.
 *
 *  [T15] Software timer — reset (xTimerReset)
 *        Task reseta o timer antes de expirar várias vezes.
 *        Timer só deve disparar depois que a task parar de resetar.
 *
 *  [T16] Stress de heap (pvPortMalloc / vPortFree)
 *        Aloca e libera blocos de tamanhos variados em loop.
 *        Testa fragmentação do heap4 e detecção de falha de alocação.
 *
 *  [T17] Múltiplas queues simultâneas
 *        Duas filas independentes com dois pares prod/cons.
 *        Testa que dados de filas diferentes não se misturam.
 *
 *  [T18] Tick counter e overflow de 32 bits (simulado)
 *        Lê xTaskGetTickCount() periodicamente e verifica monoticidade.
 *        Overflow real de 32 bits levaria ~497 dias a 100Hz, então apenas
 *        verifica que o contador avança corretamente.
 *
 *  [T19] xTaskGetCurrentTaskHandle
 *        Cada task verifica que o handle retornado bate com o seu próprio.
 *        Testa a consistência do TCB corrente durante context switch.
 *
 *  [T20] Watchdog — sistema vivo
 *        Bate a cada 2s. Se parar, algo travou o scheduler.
 */

#include <stdint.h>
#include <stddef.h>
#include "FreeRTOS.h"
#include "task.h"
#include "semphr.h"
#include "queue.h"
#include "timers.h"

/* ======================================================================
 * UART (16550 mapeada em 0x10000000)
 * ====================================================================== */
#define UART_BASE  0x10000000UL
#define UART_THR   (*(volatile uint8_t *)(UART_BASE + 0))
#define UART_LSR   (*(volatile uint8_t *)(UART_BASE + 5))
#define TX_READY   0x20

static SemaphoreHandle_t uart_mutex;

static void uart_putc(char c)
{
    while (!(UART_LSR & TX_READY)) {}
    UART_THR = c;
}

static void uart_puts_raw(const char *s)
{
    while (*s) uart_putc(*s++);
}

static void print_uint(uint32_t n)
{
    char buf[11];
    int i = 0;
    if (n == 0) { uart_putc('0'); return; }
    while (n) { buf[i++] = '0' + (n % 10); n /= 10; }
    while (i--) uart_putc(buf[i]);
}

/* Log protegido por mutex: "[TAG] tick=N msg=V\r\n" */
static void uart_log(const char *tag, const char *msg, uint32_t val)
{
    xSemaphoreTake(uart_mutex, portMAX_DELAY);
    uart_puts_raw("[");
    uart_puts_raw(tag);
    uart_puts_raw("] tick=");
    print_uint(xTaskGetTickCount());
    uart_puts_raw(" ");
    uart_puts_raw(msg);
    uart_puts_raw("=");
    print_uint(val);
    uart_puts_raw("\r\n");
    xSemaphoreGive(uart_mutex);
}

/* Log simples sem valor numérico */
static void uart_log_str(const char *tag, const char *msg)
{
    xSemaphoreTake(uart_mutex, portMAX_DELAY);
    uart_puts_raw("[");
    uart_puts_raw(tag);
    uart_puts_raw("] tick=");
    print_uint(xTaskGetTickCount());
    uart_puts_raw(" ");
    uart_puts_raw(msg);
    uart_puts_raw("\r\n");
    xSemaphoreGive(uart_mutex);
}

/* ======================================================================
 * Handles globais
 * ====================================================================== */

/* T04 */
static SemaphoreHandle_t sem_binary;

/* T05 */
static SemaphoreHandle_t sem_count;

/* T06/T07 */
static SemaphoreHandle_t mutex_shared;

/* T08/T09 */
static QueueHandle_t queue_data;

/* T10 */
static TaskHandle_t  handle_target;
static volatile uint32_t t10_target_runs;

/* T17 */
static QueueHandle_t queue_a;
static QueueHandle_t queue_b;

/* T13/T14/T15 */
static TimerHandle_t timer_oneshot;
static TimerHandle_t timer_reload;
static TimerHandle_t timer_reset;

/* ======================================================================
 * T01 — Preempção por prioridade
 *
 * BUSY_HIGH (prio 4) e BUSY_LOW (prio 1) rodam em loop ocupado contando
 * ciclos. Após 500ms, BUSY_HIGH imprime o resultado e bloqueia pra sempre.
 * BUSY_LOW só deve ter rodado se BUSY_HIGH estava bloqueado — se o contador
 * de BUSY_LOW for zero após 500ms, a preempção funcionou.
 * ====================================================================== */
static volatile uint32_t t01_high_cycles = 0;
static volatile uint32_t t01_low_cycles  = 0;

static void task_t01_high(void *p)
{
    (void)p;
    /* Roda por ~500ms ocupado */
    TickType_t start = xTaskGetTickCount();
    while ((xTaskGetTickCount() - start) < pdMS_TO_TICKS(500))
        t01_high_cycles++;

    uart_log("T01H", "high_cycles", t01_high_cycles);
    uart_log("T01H", "low_cycles_during_busy", t01_low_cycles);
    /* low_cycles deve ser 0 ou bem menor que high_cycles */
    vTaskSuspend(NULL);
}

static void task_t01_low(void *p)
{
    (void)p;
    for (;;)
        t01_low_cycles++;
}

/* ======================================================================
 * T02 — vTaskDelay
 * ====================================================================== */
static void task_t02(void *p)
{
    (void)p;
    uint32_t count = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(300));
        uart_log("T02 ", "delay_300ms", ++count);
        if (count >= 5) vTaskSuspend(NULL);
    }
}

/* ======================================================================
 * T03 — vTaskDelayUntil
 * ====================================================================== */
static void task_t03(void *p)
{
    (void)p;
    TickType_t last_wake = xTaskGetTickCount();
    uint32_t count = 0;
    for (;;) {
        vTaskDelayUntil(&last_wake, pdMS_TO_TICKS(250));
        uart_log("T03 ", "delay_until_250ms", ++count);
        if (count >= 5) vTaskSuspend(NULL);
    }
}

/* ======================================================================
 * T04 — Semáforo binário ping/pong
 * ====================================================================== */
static void task_t04_ping(void *p)
{
    (void)p;
    uint32_t round = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(400));
        uart_log("T04P", "ping", ++round);
        xSemaphoreGive(sem_binary);
        if (round >= 5) vTaskSuspend(NULL);
    }
}

static void task_t04_pong(void *p)
{
    (void)p;
    uint32_t round = 0;
    for (;;) {
        xSemaphoreTake(sem_binary, portMAX_DELAY);
        uart_log("T04G", "pong", ++round);
    }
}

/* ======================================================================
 * T05 — Semáforo de contagem
 * ====================================================================== */
static void task_t05_prod(void *p)
{
    (void)p;
    /* Dá 3 tokens de uma vez, depois espera */
    vTaskDelay(pdMS_TO_TICKS(100));
    xSemaphoreGive(sem_count);
    xSemaphoreGive(sem_count);
    xSemaphoreGive(sem_count);
    uart_log("T05P", "tokens_given", 3);
    /* Dá mais 2 após um tempo */
    vTaskDelay(pdMS_TO_TICKS(800));
    xSemaphoreGive(sem_count);
    xSemaphoreGive(sem_count);
    uart_log("T05P", "tokens_given", 2);
    vTaskSuspend(NULL);
}

static void task_t05_cons(void *p)
{
    (void)p;
    uint32_t taken = 0;
    for (;;) {
        xSemaphoreTake(sem_count, portMAX_DELAY);
        uart_log("T05C", "token_taken", ++taken);
    }
}

/* ======================================================================
 * T06 — Mutex: três tasks competindo
 * ====================================================================== */
static void task_t06_worker(void *p)
{
    const char *name = (const char *)p;
    uint32_t count = 0;
    for (;;) {
        xSemaphoreTake(mutex_shared, portMAX_DELAY);
        /* Seção crítica: imprime e simula trabalho */
        xSemaphoreTake(uart_mutex, portMAX_DELAY);
        uart_puts_raw("[T06");
        uart_puts_raw(name);
        uart_puts_raw("] dentro_mutex count=");
        print_uint(++count);
        uart_puts_raw("\r\n");
        xSemaphoreGive(uart_mutex);
        /* Simula trabalho na seção crítica */
        volatile uint32_t w = 0;
        for (uint32_t i = 0; i < 5000; i++) w++;
        (void)w;
        xSemaphoreGive(mutex_shared);
        vTaskDelay(pdMS_TO_TICKS(50));
        if (count >= 4) vTaskSuspend(NULL);
    }
}

/* ======================================================================
 * T07 — Priority Inheritance
 *
 * LOW toma o mutex e dorme (simula trabalho longo).
 * HIGH tenta tomar o mesmo mutex → LOW deve herdar prio de HIGH.
 * MED roda no meio — não deve preemptar LOW enquanto LOW tem a herança.
 * Log mostra a sequência: se MED aparecer entre LOW_HOLD e HIGH_GOT,
 * a herança não funcionou.
 * ====================================================================== */
static SemaphoreHandle_t mutex_pi; /* mutex para este teste */

static void task_t07_low(void *p)
{
    (void)p;
    vTaskDelay(pdMS_TO_TICKS(50)); /* deixa todos startarem */
    uart_log_str("T07L", "tomando_mutex");
    xSemaphoreTake(mutex_pi, portMAX_DELAY);
    uart_log_str("T07L", "segurando_mutex_inicio");
    /* Trabalho longo — HIGH vai tentar tomar o mutex aqui */
    vTaskDelay(pdMS_TO_TICKS(300));
    uart_log_str("T07L", "liberando_mutex");
    xSemaphoreGive(mutex_pi);
    vTaskSuspend(NULL);
}

static void task_t07_med(void *p)
{
    (void)p;
    vTaskDelay(pdMS_TO_TICKS(100));
    /* Se priority inheritance funcionar, esta task NÃO deve rodar
     * enquanto LOW segura o mutex e HIGH está esperando */
    uart_log_str("T07M", "rodei_durante_PI"); /* idealmente não aparece entre LOW_HOLD e HIGH_GOT */
    vTaskSuspend(NULL);
}

static void task_t07_high(void *p)
{
    (void)p;
    vTaskDelay(pdMS_TO_TICKS(150)); /* LOW já tem o mutex */
    uart_log_str("T07H", "tentando_tomar_mutex");
    xSemaphoreTake(mutex_pi, portMAX_DELAY);
    uart_log_str("T07H", "got_mutex");
    xSemaphoreGive(mutex_pi);
    vTaskSuspend(NULL);
}

/* ======================================================================
 * T08 — Queue producer/consumer com verificação de sequência
 * ====================================================================== */
static void task_t08_prod(void *p)
{
    (void)p;
    uint32_t val = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(200));
        val++;
        xQueueSend(queue_data, &val, portMAX_DELAY);
        uart_log("T08P", "enviado", val);
        if (val >= 8) vTaskSuspend(NULL);
    }
}

static void task_t08_cons(void *p)
{
    (void)p;
    uint32_t expected = 1;
    uint32_t received;
    for (;;) {
        xQueueReceive(queue_data, &received, portMAX_DELAY);
        if (received != expected) {
            uart_log("T08C", "ERRO_seq_esperado", expected);
            uart_log("T08C", "ERRO_seq_recebido", received);
        } else {
            uart_log("T08C", "ok_recebido", received);
        }
        expected++;
    }
}

/* ======================================================================
 * T09 — Queue com timeout
 * ====================================================================== */
static QueueHandle_t queue_timeout;

static void task_t09(void *p)
{
    (void)p;
    uint32_t timeouts = 0;
    uint32_t received;
    for (;;) {
        BaseType_t r = xQueueReceive(queue_timeout, &received, pdMS_TO_TICKS(300));
        if (r == pdFALSE) {
            uart_log("T09 ", "timeout", ++timeouts);
            if (timeouts >= 4) vTaskSuspend(NULL);
        } else {
            uart_log("T09 ", "recebido", received);
        }
    }
}

/* ======================================================================
 * T10 — vTaskSuspend / vTaskResume
 * ====================================================================== */
static void task_t10_target(void *p)
{
    (void)p;
    for (;;) {
        t10_target_runs++;
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

static void task_t10_ctrl(void *p)
{
    (void)p;
    /* Espera target rodar algumas vezes */
    vTaskDelay(pdMS_TO_TICKS(300));
    uint32_t runs_before = t10_target_runs;
    uart_log("T10C", "runs_before_suspend", runs_before);

    vTaskSuspend(handle_target);
    uart_log_str("T10C", "target_suspenso");

    /* Espera com target suspenso — runs não deve mudar */
    vTaskDelay(pdMS_TO_TICKS(500));
    uint32_t runs_suspended = t10_target_runs;
    uart_log("T10C", "runs_during_suspend", runs_suspended - runs_before);
    /* Deve ser 0 (ou 1 se estava no meio de um tick) */

    vTaskResume(handle_target);
    uart_log_str("T10C", "target_resumido");

    vTaskDelay(pdMS_TO_TICKS(300));
    uart_log("T10C", "runs_after_resume", t10_target_runs - runs_suspended);
    /* Deve ser > 0 */

    vTaskSuspend(NULL);
}

/* ======================================================================
 * T11 — vTaskPrioritySet / uxTaskPriorityGet
 * ====================================================================== */
static void task_t11(void *p)
{
    (void)p;
    TaskHandle_t self = xTaskGetCurrentTaskHandle();

    UBaseType_t prio = uxTaskPriorityGet(self);
    uart_log("T11 ", "prio_inicial", (uint32_t)prio);  /* deve ser 1 */

    vTaskPrioritySet(self, 3);
    prio = uxTaskPriorityGet(self);
    uart_log("T11 ", "prio_apos_set3", (uint32_t)prio); /* deve ser 3 */

    vTaskPrioritySet(self, 1);
    prio = uxTaskPriorityGet(self);
    uart_log("T11 ", "prio_apos_set1", (uint32_t)prio); /* deve ser 1 */

    vTaskSuspend(NULL);
}

/* ======================================================================
 * T12 — vTaskDelete
 * ====================================================================== */
static void task_t12(void *p)
{
    (void)p;
    uart_log_str("T12 ", "rodei_uma_vez_deletando");
    vTaskDelete(NULL);
    /* Nunca deve aparecer uma segunda mensagem daqui */
}

/* ======================================================================
 * T13 — Software timer one-shot
 * ====================================================================== */
static void cb_timer_oneshot(TimerHandle_t xTimer)
{
    (void)xTimer;
    /* Callback roda no contexto da timer task — não pode usar uart_mutex
     * com portMAX_DELAY em contexto de timer, mas aqui é seguro pois
     * uart_log usa xSemaphoreTake com portMAX_DELAY que é permitido
     * dentro da timer task (ela é uma task normal do FreeRTOS) */
    uart_log("T13 ", "oneshot_disparou", xTaskGetTickCount());
}

/* ======================================================================
 * T14 — Software timer auto-reload
 * ====================================================================== */
static void cb_timer_reload(TimerHandle_t xTimer)
{
    (void)xTimer;
    static uint32_t fires = 0;
    uart_log("T14 ", "reload_disparou", ++fires);
    if (fires >= 4) {
        xTimerStop(xTimer, 0);
        uart_log_str("T14 ", "timer_parado");
    }
}

/* ======================================================================
 * T15 — Software timer com reset
 * ====================================================================== */
static void cb_timer_reset(TimerHandle_t xTimer)
{
    (void)xTimer;
    uart_log_str("T15 ", "timer_reset_disparou"); /* só deve aparecer 1x */
}

static void task_t15_resetter(void *p)
{
    (void)p;
    /* Reseta o timer 3 vezes antes de deixar expirar */
    for (int i = 0; i < 3; i++) {
        vTaskDelay(pdMS_TO_TICKS(200));
        xTimerReset(timer_reset, 0);
        uart_log("T15R", "reset_numero", (uint32_t)(i + 1));
    }
    /* Agora deixa expirar */
    vTaskSuspend(NULL);
}

/* ======================================================================
 * T16 — Stress de heap
 * ====================================================================== */
static void task_t16_heap(void *p)
{
    (void)p;
    uint32_t allocs = 0;
    /* Aloca e libera blocos de tamanhos diferentes para estressar heap4 */
    static const size_t sizes[] = { 16, 64, 128, 32, 256, 48, 100, 200 };
    void *ptrs[8];

    for (uint32_t round = 0; round < 3; round++) {
        /* Aloca todos */
        for (int i = 0; i < 8; i++) {
            ptrs[i] = pvPortMalloc(sizes[i]);
            if (ptrs[i] == NULL) {
                uart_log("T16 ", "ERRO_malloc_falhou_size", (uint32_t)sizes[i]);
            } else {
                allocs++;
            }
        }
        /* Libera em ordem diferente (testa coalescência) */
        vPortFree(ptrs[3]);
        vPortFree(ptrs[0]);
        vPortFree(ptrs[6]);
        vPortFree(ptrs[1]);
        vPortFree(ptrs[4]);
        vPortFree(ptrs[7]);
        vPortFree(ptrs[2]);
        vPortFree(ptrs[5]);

        uart_log("T16 ", "round_ok", round + 1);
        vTaskDelay(pdMS_TO_TICKS(100));
    }

    uart_log("T16 ", "total_allocs_ok", allocs);
    uart_log("T16 ", "free_heap_final", (uint32_t)xPortGetFreeHeapSize());
    vTaskSuspend(NULL);
}

/* ======================================================================
 * T17 — Duas queues simultâneas e independentes
 * ====================================================================== */
static void task_t17_prod_a(void *p)
{
    (void)p;
    uint32_t val = 0xA000;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(180));
        val++;
        xQueueSend(queue_a, &val, portMAX_DELAY);
        uart_log("T17A", "prod_a", val);
        if ((val & 0xFF) >= 5) vTaskSuspend(NULL);
    }
}

static void task_t17_prod_b(void *p)
{
    (void)p;
    uint32_t val = 0xB000;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(220));
        val++;
        xQueueSend(queue_b, &val, portMAX_DELAY);
        uart_log("T17B", "prod_b", val);
        if ((val & 0xFF) >= 5) vTaskSuspend(NULL);
    }
}

static void task_t17_cons_a(void *p)
{
    (void)p;
    uint32_t received;
    for (;;) {
        xQueueReceive(queue_a, &received, portMAX_DELAY);
        /* Verifica que veio da fila A (prefixo 0xA000) */
        if ((received & 0xF000) != 0xA000)
            uart_log("T17A", "ERRO_fila_cruzada", received);
        else
            uart_log("T17A", "cons_a_ok", received & 0xFF);
    }
}

static void task_t17_cons_b(void *p)
{
    (void)p;
    uint32_t received;
    for (;;) {
        xQueueReceive(queue_b, &received, portMAX_DELAY);
        if ((received & 0xF000) != 0xB000)
            uart_log("T17B", "ERRO_fila_cruzada", received);
        else
            uart_log("T17B", "cons_b_ok", received & 0xFF);
    }
}

/* ======================================================================
 * T18 — Tick counter: monoticidade
 * ====================================================================== */
static void task_t18(void *p)
{
    (void)p;
    TickType_t prev = xTaskGetTickCount();
    uint32_t errors = 0;
    for (uint32_t i = 0; i < 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        TickType_t now = xTaskGetTickCount();
        if (now <= prev) {
            uart_log("T18 ", "ERRO_nao_monotico", (uint32_t)now);
            errors++;
        }
        prev = now;
    }
    uart_log("T18 ", "tick_final", (uint32_t)prev);
    uart_log("T18 ", "erros_monoticidade", errors);
    vTaskSuspend(NULL);
}

/* ======================================================================
 * T19 — xTaskGetCurrentTaskHandle
 * ====================================================================== */
static TaskHandle_t handle_t19_a;
static TaskHandle_t handle_t19_b;

static void task_t19_a(void *p)
{
    (void)p;
    uint32_t errors = 0;
    for (uint32_t i = 0; i < 5; i++) {
        if (xTaskGetCurrentTaskHandle() != handle_t19_a) errors++;
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    uart_log("T19A", "handle_erros", errors); /* deve ser 0 */
    vTaskSuspend(NULL);
}

static void task_t19_b(void *p)
{
    (void)p;
    uint32_t errors = 0;
    for (uint32_t i = 0; i < 5; i++) {
        if (xTaskGetCurrentTaskHandle() != handle_t19_b) errors++;
        vTaskDelay(pdMS_TO_TICKS(130));
    }
    uart_log("T19B", "handle_erros", errors); /* deve ser 0 */
    vTaskSuspend(NULL);
}

/* ======================================================================
 * T20 — Watchdog
 * ====================================================================== */
static void task_t20_watchdog(void *p)
{
    (void)p;
    uint32_t beats = 0;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        uart_log("T20 ", "watchdog_beat", ++beats);
    }
}

/* ======================================================================
 * main
 * ====================================================================== */
int main(void)
{
    uart_puts_raw("\r\n");
    uart_puts_raw("========================================\r\n");
    uart_puts_raw("  FreeRTOS Full Test Suite - RISC-V     \r\n");
    uart_puts_raw("========================================\r\n");
    uart_puts_raw("Criando primitivas...\r\n");

    /* Mutex da UART — usado por todos os testes */
    uart_mutex   = xSemaphoreCreateMutex();

    /* T04 */
    sem_binary   = xSemaphoreCreateBinary();

    /* T05 — contagem máxima 10 */
    sem_count    = xSemaphoreCreateCounting(10, 0);

    /* T06 */
    mutex_shared = xSemaphoreCreateMutex();

    /* T07 */
    mutex_pi     = xSemaphoreCreateMutex();

    /* T08 */
    queue_data   = xQueueCreate(4, sizeof(uint32_t));

    /* T09 */
    queue_timeout = xQueueCreate(2, sizeof(uint32_t));

    /* T17 */
    queue_a = xQueueCreate(4, sizeof(uint32_t));
    queue_b = xQueueCreate(4, sizeof(uint32_t));

    /* T13 — one-shot 300ms */
    timer_oneshot = xTimerCreate("T13", pdMS_TO_TICKS(300), pdFALSE, NULL, cb_timer_oneshot);

    /* T14 — auto-reload 400ms */
    timer_reload  = xTimerCreate("T14", pdMS_TO_TICKS(400), pdTRUE,  NULL, cb_timer_reload);

    /* T15 — one-shot 500ms (vai ser resetado várias vezes) */
    timer_reset   = xTimerCreate("T15", pdMS_TO_TICKS(500), pdFALSE, NULL, cb_timer_reset);

    uart_puts_raw("Criando tasks...\r\n");

    /* T01 — preempção */
    xTaskCreate(task_t01_high, "T01H", 256, NULL, 4, NULL);
    xTaskCreate(task_t01_low,  "T01L", 256, NULL, 1, NULL);

    /* T02 — vTaskDelay */
    xTaskCreate(task_t02, "T02", 256, NULL, 2, NULL);

    /* T03 — vTaskDelayUntil */
    xTaskCreate(task_t03, "T03", 256, NULL, 2, NULL);

    /* T04 — semáforo binário */
    xTaskCreate(task_t04_ping, "T04P", 256, NULL, 2, NULL);
    xTaskCreate(task_t04_pong, "T04G", 256, NULL, 2, NULL);

    /* T05 — semáforo de contagem */
    xTaskCreate(task_t05_prod, "T05P", 256, NULL, 2, NULL);
    xTaskCreate(task_t05_cons, "T05C", 256, NULL, 2, NULL);

    /* T06 — mutex compartilhado por 3 workers */
    xTaskCreate(task_t06_worker, "T06A", 256, (void *)"A", 2, NULL);
    xTaskCreate(task_t06_worker, "T06B", 256, (void *)"B", 2, NULL);
    xTaskCreate(task_t06_worker, "T06C", 256, (void *)"C", 2, NULL);

    /* T07 — priority inheritance */
    xTaskCreate(task_t07_low,  "T07L", 256, NULL, 1, NULL);
    xTaskCreate(task_t07_med,  "T07M", 256, NULL, 2, NULL);
    xTaskCreate(task_t07_high, "T07H", 256, NULL, 3, NULL);

    /* T08 — queue com verificação de sequência */
    xTaskCreate(task_t08_prod, "T08P", 256, NULL, 2, NULL);
    xTaskCreate(task_t08_cons, "T08C", 256, NULL, 2, NULL);

    /* T09 — queue com timeout */
    xTaskCreate(task_t09, "T09", 256, NULL, 2, NULL);

    /* T10 — suspend/resume */
    xTaskCreate(task_t10_target, "T10T", 256, NULL, 2, &handle_target);
    xTaskCreate(task_t10_ctrl,   "T10C", 256, NULL, 2, NULL);

    /* T11 — vTaskPrioritySet */
    xTaskCreate(task_t11, "T11", 256, NULL, 1, NULL);

    /* T12 — vTaskDelete */
    xTaskCreate(task_t12, "T12", 256, NULL, 1, NULL);

    /* T15 — timer reset (task resetter) */
    xTaskCreate(task_t15_resetter, "T15R", 256, NULL, 2, NULL);

    /* T16 — heap stress */
    xTaskCreate(task_t16_heap, "T16", 512, NULL, 1, NULL);

    /* T17 — duas queues */
    xTaskCreate(task_t17_prod_a, "T17PA", 256, NULL, 2, NULL);
    xTaskCreate(task_t17_prod_b, "T17PB", 256, NULL, 2, NULL);
    xTaskCreate(task_t17_cons_a, "T17CA", 256, NULL, 2, NULL);
    xTaskCreate(task_t17_cons_b, "T17CB", 256, NULL, 2, NULL);

    /* T18 — tick counter */
    xTaskCreate(task_t18, "T18", 256, NULL, 1, NULL);

    /* T19 — current task handle */
    xTaskCreate(task_t19_a, "T19A", 256, NULL, 2, &handle_t19_a);
    xTaskCreate(task_t19_b, "T19B", 256, NULL, 2, &handle_t19_b);

    /* T20 — watchdog */
    xTaskCreate(task_t20_watchdog, "T20", 256, NULL, 1, NULL);

    /* Inicia os software timers (após o scheduler, mas xTimerStart antes é ok
     * pois os comandos ficam na fila até a timer task iniciar) */
    xTimerStart(timer_oneshot, 0);
    xTimerStart(timer_reload,  0);
    xTimerStart(timer_reset,   0);

    uart_puts_raw("Iniciando scheduler...\r\n");
    vTaskStartScheduler();

    /* Nunca deve chegar aqui */
    uart_puts_raw("ERRO FATAL: scheduler retornou!\r\n");
    for (;;);
}