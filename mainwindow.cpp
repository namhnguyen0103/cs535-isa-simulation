#include "mainwindow.hpp"
#include "parser.hpp"

#include <QFileDialog>
#include <QMessageBox>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QFormLayout>
#include <QGroupBox>
#include <QFont>
#include <QFileInfo>
#include <QFrame>

#include <algorithm>
#include <iomanip>
#include <sstream>

static const char* C_IF       = "#AED6F1";
static const char* C_ID       = "#A9DFBF";
static const char* C_EX       = "#F9E79F";
static const char* C_MEM      = "#F5CBA7";
static const char* C_WB       = "#D7BDE2";
static const char* C_SQUASHED = "#F1948A";
static const char* C_IDLE     = "#E5E7E9";

static constexpr int MAX_CYCLES = 100000;

static QString hexStr(int v) {
    std::ostringstream ss;
    ss << "0x" << std::hex << std::setw(8) << std::setfill('0') << v;
    return QString::fromStdString(ss.str());
}

QString MainWindow::registerName(int reg) {
    switch (reg) {
        case  0: return "r0  (zero  — hardwired 0)";
        case  1: return "r1  (ra    — return address)";
        case  2: return "r2  (sp    — stack pointer)";
        case 31: return "r31 (flags — status bits)";
        default: return QString("r%1 ").arg(reg, 2);
    }
}

QString MainWindow::flagsDescription(int v) {
    QStringList bits;
    if (v & FLAG_ZERO)     bits << "Z";
    if (v & FLAG_NEGATIVE) bits << "N";
    if (v & FLAG_OVERFLOW) bits << "OV";
    if (v & FLAG_DIVZERO)  bits << "DZ";
    return bits.isEmpty() ? "(none)" : bits.join(" | ");
}

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
    setWindowTitle("CS 535 CPU Pipeline Simulator");
    resize(1450, 880);
    setupUI();
}

void MainWindow::setupUI() {
    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout* root = new QVBoxLayout(central);
    root->setSpacing(6);
    root->setContentsMargins(8, 8, 8, 8);

    // -----------------------------------------------------------------------
    // 1. Mode selector
    // -----------------------------------------------------------------------
    QGroupBox* modeBox = new QGroupBox("Simulation Mode");
    QVBoxLayout* modeVL = new QVBoxLayout(modeBox);
    QHBoxLayout* modeHL = new QHBoxLayout();

    modeNoPipeNoCache_ = new QRadioButton("No Pipe / No Cache");
    modePipeOnly_      = new QRadioButton("Pipeline Only  (no cache)");
    modeCacheOnly_     = new QRadioButton("Cache Only  (no pipeline)");
    modeBoth_          = new QRadioButton("Pipeline + Cache  (full)");
    modeBoth_->setChecked(true);

    modeGroup_ = new QButtonGroup(this);
    modeGroup_->addButton(modeNoPipeNoCache_, 0);
    modeGroup_->addButton(modePipeOnly_,      1);
    modeGroup_->addButton(modeCacheOnly_,     2);
    modeGroup_->addButton(modeBoth_,          3);

    modeDescLabel_ = new QLabel(
        "Full simulation: 5-stage pipeline + cache. All 5 stages run concurrently.");
    modeDescLabel_->setStyleSheet("color: #444; font-size: 9pt; font-style: italic;");

    modeHL->addWidget(modeNoPipeNoCache_);
    modeHL->addWidget(modePipeOnly_);
    modeHL->addWidget(modeCacheOnly_);
    modeHL->addWidget(modeBoth_);
    modeHL->addStretch();
    modeVL->addLayout(modeHL);
    modeVL->addWidget(modeDescLabel_);
    root->addWidget(modeBox);

    connect(modeGroup_, QOverload<int>::of(&QButtonGroup::idClicked), [this](int id) {
        static const char* descs[4] = {
            "No pipeline: one instruction at a time through all 5 stages. Memory hits DRAM directly. Each instruction takes ≥5 cycles.",
            "No pipeline: one instruction at a time through all 5 stages. Memory hits DRAM directly. Each instruction takes ≥5 cycles.",
            "No pipeline: one instruction at a time through all 5 stages. Memory goes through cache. Each instruction takes ≥5 cycles.",
            "Full pipeline + cache: all 5 stages run concurrently. Shows data hazard stalls and branch squashes."
        };
        modeDescLabel_->setText(descs[id]);
        currentMode_ = static_cast<SimMode>(id);
        bool useCache = (id == 2 || id == 3);
        cachePanel_->setVisible(useCache);
        cacheCfgBox_->setVisible(useCache);
        simReady_ = false;
        stepCycleBtn_->setEnabled(false);
        stepInstrBtn_->setEnabled(false);
        runBtn_->setEnabled(false);
        statusLabel_->setText("Mode changed — reload your file to apply.");
        refreshInfoBar();
    });

    // -----------------------------------------------------------------------
    // 2. Info bar
    // -----------------------------------------------------------------------
    QFrame* infoBar = new QFrame();
    infoBar->setFrameShape(QFrame::StyledPanel);
    infoBar->setStyleSheet("QFrame { background: #F4F6F7; border-radius: 6px; }");
    QHBoxLayout* infoHL = new QHBoxLayout(infoBar);
    infoHL->setContentsMargins(12, 6, 12, 6);

    auto makeCounter = [&](const QString& labelText, QLabel*& valueLabel,
                           const QString& tooltip) {
        QVBoxLayout* vl = new QVBoxLayout();
        QLabel* title = new QLabel(labelText);
        title->setStyleSheet("font-size: 9pt; color: #666; font-weight: bold;");
        title->setAlignment(Qt::AlignCenter);
        valueLabel = new QLabel("—");
        valueLabel->setAlignment(Qt::AlignCenter);
        valueLabel->setToolTip(tooltip);
        valueLabel->setMinimumWidth(120);
        QFont vf = valueLabel->font();
        vf.setPointSize(18); vf.setBold(true);
        valueLabel->setFont(vf);
        valueLabel->setStyleSheet("color: #1A5276;");
        vl->addWidget(title);
        vl->addWidget(valueLabel);
        return vl;
    };

    infoHL->addLayout(makeCounter("CLOCK CYCLE", cycleValueLabel_,
        "Total clock cycles elapsed."));
    QFrame* sep = new QFrame();
    sep->setFrameShape(QFrame::VLine);
    sep->setStyleSheet("color: #ccc;");
    infoHL->addWidget(sep);
    infoHL->addLayout(makeCounter("PROGRAM COUNTER (PC)", pcValueLabel_,
        "Address of the next instruction to be fetched."));
    infoHL->addStretch();
    root->addWidget(infoBar);

    // -----------------------------------------------------------------------
    // 3. Pipeline stage boxes — shown for ALL four modes
    // -----------------------------------------------------------------------
    QWidget* pipeWidget = new QWidget();
    QHBoxLayout* pipeHL = new QHBoxLayout(pipeWidget);
    pipeHL->setContentsMargins(0,0,0,0);

    auto makeStageBox = [&](const QString& title, QLabel*& lbl) {
        QGroupBox* box = new QGroupBox(title);
        box->setMinimumWidth(175);
        QVBoxLayout* vl = new QVBoxLayout(box);
        lbl = new QLabel("---");
        lbl->setWordWrap(true);
        lbl->setAlignment(Qt::AlignCenter);
        QFont f = lbl->font(); f.setPointSize(9); lbl->setFont(f);
        vl->addWidget(lbl);
        pipeHL->addWidget(box);
    };
    makeStageBox("IF  – Fetch",      stageIF_);
    makeStageBox("ID  – Decode",     stageID_);
    makeStageBox("EX  – Execute",    stageEX_);
    makeStageBox("MEM – Memory",     stageMEM_);
    makeStageBox("WB  – Write Back", stageWB_);
    pipeHL->addStretch();
    for (auto* l : {stageIF_, stageID_, stageEX_, stageMEM_, stageWB_})
        setStageStyle(l, C_IDLE, "---");
    root->addWidget(pipeWidget);

    // -----------------------------------------------------------------------
    // 4. Data panels
    // -----------------------------------------------------------------------
    QHBoxLayout* panelsHL = new QHBoxLayout();
    panelsHL->setSpacing(6);
    auto makePanel = [&](const QString& title, QListWidget*& list, QGroupBox** outBox = nullptr) {
        QGroupBox* box = new QGroupBox(title);
        QVBoxLayout* vl = new QVBoxLayout(box);
        list = new QListWidget();
        list->setFont(QFont("Courier", 9));
        list->setSelectionMode(QAbstractItemView::NoSelection);
        vl->addWidget(list);
        panelsHL->addWidget(box);
        if (outBox) *outBox = box;
    };
    makePanel("Instructions", instrList_);
    makePanel("Registers",    regList_);
    makePanel("Cache",        cacheList_, &cachePanel_);
    makePanel("DRAM",         dramList_);
    root->addLayout(panelsHL, 1);

    // -----------------------------------------------------------------------
    // 5. Bottom: memory config + controls
    // -----------------------------------------------------------------------
    QHBoxLayout* bottomHL = new QHBoxLayout();
    bottomHL->setSpacing(10);

    QGroupBox* dramCfg = new QGroupBox("DRAM Configuration");
    QFormLayout* dramForm = new QFormLayout(dramCfg);
    dramNumLinesSpin_ = new QSpinBox(); dramNumLinesSpin_->setRange(4,512); dramNumLinesSpin_->setValue(64);
    dramLineSizeSpin_ = new QSpinBox(); dramLineSizeSpin_->setRange(1,32);  dramLineSizeSpin_->setValue(4);
    dramDelaySpin_    = new QSpinBox(); dramDelaySpin_->setRange(0,50);     dramDelaySpin_->setValue(3);
    dramCapLabel_ = new QLabel();
    dramCapLabel_->setStyleSheet("color: #555; font-size: 9pt;");
    auto updateCap = [this]() {
        int total = dramNumLinesSpin_->value() * dramLineSizeSpin_->value();
        dramCapLabel_->setText(
            QString("Total: %1 words  (addresses 0–%2)").arg(total).arg(total-1));
    };
    connect(dramNumLinesSpin_, QOverload<int>::of(&QSpinBox::valueChanged), [=](int){ updateCap(); });
    connect(dramLineSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), [=](int v){
        if(cacheLineSizeSpin_) cacheLineSizeSpin_->setValue(v); updateCap(); });
    dramForm->addRow("Num Lines:",   dramNumLinesSpin_);
    dramForm->addRow("Line Size:",   dramLineSizeSpin_);
    dramForm->addRow("Delay (cyc):", dramDelaySpin_);
    dramForm->addRow("",             dramCapLabel_);
    updateCap();

    cacheCfgBox_ = new QGroupBox("Cache Configuration");
    QFormLayout* cacheForm = new QFormLayout(cacheCfgBox_);
    cacheNumLinesSpin_ = new QSpinBox(); cacheNumLinesSpin_->setRange(1,64); cacheNumLinesSpin_->setValue(4);
    cacheLineSizeSpin_ = new QSpinBox(); cacheLineSizeSpin_->setRange(1,32); cacheLineSizeSpin_->setValue(4);
    cacheLineSizeSpin_->setEnabled(false);
    cacheDelaySpin_    = new QSpinBox(); cacheDelaySpin_->setRange(0,20);    cacheDelaySpin_->setValue(1);
    cacheForm->addRow("Num Lines:",          cacheNumLinesSpin_);
    cacheForm->addRow("Line Size (= DRAM):", cacheLineSizeSpin_);
    cacheForm->addRow("Delay (cyc):",        cacheDelaySpin_);

    QVBoxLayout* btnVL = new QVBoxLayout();
    QPushButton* loadBtn = new QPushButton("Load Instructions File…");
    loadBtn->setMinimumWidth(210);

    stepCycleBtn_ = new QPushButton("Step One Cycle  ▶");
    stepCycleBtn_->setMinimumWidth(210);
    stepCycleBtn_->setEnabled(false);
    stepCycleBtn_->setToolTip("Advance exactly one clock cycle.");

    stepInstrBtn_ = new QPushButton("Step One Instruction  ▶|");
    stepInstrBtn_->setMinimumWidth(210);
    stepInstrBtn_->setEnabled(false);
    stepInstrBtn_->setToolTip(
        "Advance until one complete instruction exits WB.\n"
        "In no-pipeline modes this is always ≥5 cycles.\n"
        "In pipeline mode this advances to the next WB completion.");

    runBtn_ = new QPushButton("Run to Completion  ▶▶");
    runBtn_->setMinimumWidth(210);
    runBtn_->setEnabled(false);

    statusLabel_ = new QLabel("No program loaded.");
    statusLabel_->setStyleSheet("color: #555;");
    statusLabel_->setWordWrap(true);

    btnVL->addWidget(loadBtn);
    btnVL->addSpacing(4);
    btnVL->addWidget(stepCycleBtn_);
    btnVL->addWidget(stepInstrBtn_);
    btnVL->addWidget(runBtn_);
    btnVL->addStretch();
    btnVL->addWidget(statusLabel_);

    bottomHL->addWidget(dramCfg);
    bottomHL->addWidget(cacheCfgBox_);
    bottomHL->addLayout(btnVL);
    root->addLayout(bottomHL);

    connect(loadBtn,       &QPushButton::clicked, this, &MainWindow::onLoadFile);
    connect(stepCycleBtn_, &QPushButton::clicked, this, &MainWindow::onStepCycle);
    connect(stepInstrBtn_, &QPushButton::clicked, this, &MainWindow::onStepInstruction);
    connect(runBtn_,       &QPushButton::clicked, this, &MainWindow::onRunToCompletion);
}

// ---------------------------------------------------------------------------
// initSimulator
// ---------------------------------------------------------------------------
void MainWindow::initSimulator(const std::vector<Instruction>&               program,
                               const std::vector<std::pair<int,DRAM::Line>>& dataBlocks) {
    pipeline_.reset(); cache_.reset(); directMem_.reset();

    int dramLines  = dramNumLinesSpin_->value();
    int lineSize   = dramLineSizeSpin_->value();
    int dramDelay  = dramDelaySpin_->value();
    int cacheLines = cacheNumLinesSpin_->value();
    int cacheDelay = cacheDelaySpin_->value();

    dram_ = std::make_unique<DRAM>(dramLines, lineSize, dramDelay);
    loadProgramToDRAM(program, *dram_, PROGRAM_BASE);
    for (auto& [addr, line] : dataBlocks) {
        DRAM::Line padded(lineSize, 0);
        for (int i = 0; i < std::min((int)line.size(), lineSize); ++i) padded[i] = line[i];
        dram_->setLineDirect(addr, padded);
    }

    bool useCache   = (currentMode_ == SimMode::CACHE_ONLY || currentMode_ == SimMode::BOTH);
    bool noOverlap  = (currentMode_ == SimMode::NO_PIPE_NO_CACHE ||
                       currentMode_ == SimMode::CACHE_ONLY);

    MemIF* mem = nullptr;
    if (useCache) {
        cache_ = std::make_unique<Cache>(cacheLines, lineSize, cacheDelay, dram_.get(),
            Cache::WritePolicy::WRITE_BACK, Cache::AllocatePolicy::WRITE_ALLOCATE);
        mem = cache_.get();
    } else {
        directMem_ = std::make_unique<DirectMemIF>(dram_.get());
        mem = directMem_.get();
    }

    // All four modes use Pipeline — noOverlap=true prevents instruction overlap
    pipeline_ = std::make_unique<Pipeline>(
        PROGRAM_BASE, (int)program.size(), mem, noOverlap);

    simReady_ = true;
    stepCycleBtn_->setEnabled(true);
    stepInstrBtn_->setEnabled(true);
    runBtn_->setEnabled(true);
    refreshAll();
}

// ---------------------------------------------------------------------------
// onLoadFile
// ---------------------------------------------------------------------------
void MainWindow::onLoadFile() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open Instructions File", "", "Text Files (*.txt);;All Files (*)");
    if (path.isEmpty()) return;

    try {
        ParseResult parsed = parseInstructionFile(path.toStdString());
        if (parsed.program.empty()) {
            QMessageBox::warning(this, "Empty Program", "No instructions found."); return;
        }
        int numInstrs  = (int)parsed.program.size();
        int totalWords = dramNumLinesSpin_->value() * dramLineSizeSpin_->value();
        for (auto& [addr, line] : parsed.dataBlocks) {
            int norm = addr % totalWords;
            if (norm < numInstrs)
                QMessageBox::warning(this, "Address Collision",
                    QString("DATA at address %1 (→ %2) overlaps instructions (0–%3).")
                    .arg(addr).arg(norm).arg(numInstrs-1));
        }
        program_ = parsed.program;
        initSimulator(parsed.program, parsed.dataBlocks);
        statusLabel_->setText(
            QString("Loaded: %1   [%2]")
            .arg(QFileInfo(path).fileName())
            .arg(QString::fromStdString(simModeName(currentMode_))));
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, "Parse Error", QString::fromStdString(ex.what()));
    }
}

// ---------------------------------------------------------------------------
// onStepCycle — advance exactly one clock cycle
// ---------------------------------------------------------------------------
void MainWindow::onStepCycle() {
    if (!simReady_ || !pipeline_) return;
    pipeline_->tick();
    refreshAll();
    checkDone();
}

// ---------------------------------------------------------------------------
// onStepInstruction — advance until one instruction completes WB
// ---------------------------------------------------------------------------
void MainWindow::onStepInstruction() {
    if (!simReady_ || !pipeline_) return;
    pipeline_->stepInstruction();
    refreshAll();
    checkDone();
}

// ---------------------------------------------------------------------------
// onRunToCompletion
// ---------------------------------------------------------------------------
void MainWindow::onRunToCompletion() {
    if (!simReady_ || !pipeline_) return;

    int cyclesRun = 0;
    bool hitLimit = false;
    while (!pipeline_->isDone()) {
        pipeline_->tick();
        if (++cyclesRun > MAX_CYCLES) { hitLimit = true; break; }
    }

    refreshAll();

    if (hitLimit) {
        QMessageBox::warning(this, "Cycle Limit Reached",
            QString("Simulation did not complete after %1 cycles.\n"
                    "This may indicate an infinite loop in your program.")
            .arg(MAX_CYCLES));
        statusLabel_->setText(
            QString("Stopped at cycle limit (%1 cycles)  [%2]")
            .arg(pipeline_->getCycleCount())
            .arg(QString::fromStdString(simModeName(currentMode_))));
        return;
    }

    checkDone();
}

// ---------------------------------------------------------------------------
// checkDone
// ---------------------------------------------------------------------------
void MainWindow::checkDone() {
    if (!pipeline_ || !pipeline_->isDone()) return;
    stepCycleBtn_->setEnabled(false);
    stepInstrBtn_->setEnabled(false);
    runBtn_->setEnabled(false);
    statusLabel_->setText(
        QString("Done — %1 cycles  [%2]")
        .arg(pipeline_->getCycleCount())
        .arg(QString::fromStdString(simModeName(currentMode_))));
}

// ---------------------------------------------------------------------------
// refreshAll
// ---------------------------------------------------------------------------
void MainWindow::refreshAll() {
    refreshPipelineDisplay();
    refreshInfoBar();
    refreshInstructions();
    refreshRegisters();
    refreshCache();
    refreshDRAM();
}

void MainWindow::refreshInfoBar() {
    if (!simReady_ || !pipeline_) {
        cycleValueLabel_->setText("—");
        pcValueLabel_->setText("—");
        return;
    }
    int cycle = pipeline_->getCycleCount();
    int pc    = pipeline_->getCurrentPC();
    cycleValueLabel_->setText(QString::number(cycle));
    pcValueLabel_->setText(
        QString("%1  <span style='font-size:10pt; color:#555;'>(0x%2)</span>")
        .arg(pc).arg(pc, 4, 16, QChar('0')));
    pcValueLabel_->setTextFormat(Qt::RichText);
}

void MainWindow::refreshPipelineDisplay() {
    if (!pipeline_) return;
    auto color = [](const std::string& lbl, const char* normal) -> const char* {
        if (lbl.find("[squashed]") != std::string::npos) return C_SQUASHED;
        if (lbl == "---") return C_IDLE;
        return normal;
    };
    auto q = [](const std::string& s){ return QString::fromStdString(s); };
    setStageStyle(stageIF_,  color(pipeline_->getIFLabel(),  C_IF),  q(pipeline_->getIFLabel()));
    setStageStyle(stageID_,  color(pipeline_->getIDLabel(),  C_ID),  q(pipeline_->getIDLabel()));
    setStageStyle(stageEX_,  color(pipeline_->getEXLabel(),  C_EX),  q(pipeline_->getEXLabel()));
    setStageStyle(stageMEM_, color(pipeline_->getMEMLabel(), C_MEM), q(pipeline_->getMEMLabel()));
    setStageStyle(stageWB_,  color(pipeline_->getWBLabel(),  C_WB),  q(pipeline_->getWBLabel()));
}

void MainWindow::refreshInstructions() {
    instrList_->clear();
    if (program_.empty() || !pipeline_) return;
    auto getColor = [&](int pc) -> const char* {
        if (pc == pipeline_->getWBPC())  return C_WB;
        if (pc == pipeline_->getMEMPC()) return C_MEM;
        if (pc == pipeline_->getEXPC())  return C_EX;
        if (pc == pipeline_->getIDPC())  return C_ID;
        if (pc == pipeline_->getIFPC())  return C_IF;
        return nullptr;
    };
    for (int i = 0; i < (int)program_.size(); ++i) {
        int pc = PROGRAM_BASE + i;
        auto* item = new QListWidgetItem(
            QString("%1: %2").arg(i,3).arg(QString::fromStdString(program_[i].label)));
        const char* c = getColor(pc);
        if (c) {
            item->setBackground(QColor(c));
            QFont f = instrList_->font(); f.setBold(true); item->setFont(f);
            instrList_->scrollToItem(item);
        }
        instrList_->addItem(item);
    }
}

void MainWindow::refreshRegisters() {
    regList_->clear();
    if (!pipeline_) return;
    for (int i = 0; i < NUM_REGS; ++i) {
        int v = pipeline_->readRegister(i);
        QString line = (i == REG_FLAGS)
            ? QString("%1= %2  flags: %3").arg(registerName(i)).arg(hexStr(v)).arg(flagsDescription(v))
            : QString("%1= %2  (%3)").arg(registerName(i)).arg(v).arg(hexStr(v));
        auto* item = new QListWidgetItem(line);
        if (i == REG_ZERO || i == REG_RA || i == REG_SP || i == REG_FLAGS) {
            item->setForeground(QColor("#1A5276"));
            QFont f = regList_->font(); f.setBold(true); item->setFont(f);
        } else if (v != 0) { item->setForeground(Qt::darkBlue); }
        else                { item->setForeground(Qt::gray); }
        regList_->addItem(item);
    }
}

void MainWindow::refreshCache() {
    cacheList_->clear();
    if (!cache_) return;
    for (int i = 0; i < cache_->getNumLines(); ++i) {
        auto info = cache_->getCacheLine(i);
        QString status = QString("Line %1 | V=%2 D=%3 T=%4 |")
            .arg(i,2).arg(info.valid?1:0).arg(info.dirty?1:0).arg(info.tag);
        QStringList words;
        for (int w : info.data) words << hexStr(w);
        auto* item = new QListWidgetItem(status + " " + words.join(" "));
        if (info.valid)
            item->setForeground(info.dirty ? QColor("#C0392B") : QColor("#1A5276"));
        else item->setForeground(Qt::gray);
        cacheList_->addItem(item);
    }
}

void MainWindow::refreshDRAM() {
    dramList_->clear();
    if (!dram_) return;
    int numLines  = dram_->getNumLines();
    int lineSize  = dram_->getLineSize();
    int progLines = ((int)program_.size() + lineSize - 1) / lineSize;

    for (int i = 0; i < numLines; ++i) {
        DRAM::Line line = dram_->peekLine(i * lineSize);
        QStringList words;
        for (int w : line) words << hexStr(w);

        bool isInstr = (i < progLines);
        // A data line is any non-instruction line containing at least one non-zero word.
        // This highlights all DATA blocks regardless of how many lines they span.
        bool isData  = !isInstr &&
                       std::any_of(line.begin(), line.end(), [](int w){ return w != 0; });

        QString tag = isInstr ? " [instr]" : (isData ? " [data]" : "");
        auto* item = new QListWidgetItem(
            QString("Line %1%2 | %3").arg(i,2).arg(tag).arg(words.join(" ")));

        if (isInstr)     item->setForeground(QColor("#6C3483"));  // purple
        else if (isData) item->setForeground(QColor("#1A5276"));  // blue

        dramList_->addItem(item);
    }
}

void MainWindow::setStageStyle(QLabel* label, const QString& color, const QString& text) {
    label->setText(text);
    label->setStyleSheet(
        QString("background-color: %1; border-radius: 4px; padding: 4px;").arg(color));
}
