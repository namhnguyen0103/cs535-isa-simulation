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

#include <iomanip>
#include <sstream>

static const char* C_IF       = "#AED6F1";
static const char* C_ID       = "#A9DFBF";
static const char* C_EX       = "#F9E79F";
static const char* C_MEM      = "#F5CBA7";
static const char* C_WB       = "#D7BDE2";
static const char* C_SQUASHED = "#F1948A";
static const char* C_SEQ      = "#D5F5E3";
static const char* C_IDLE     = "#E5E7E9";

// Safety limit — if the simulation hasn't finished after this many cycles,
// stop and warn the user rather than hanging the UI thread.
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
        "Full simulation: 5-stage pipeline + cache. Shows stalls, squashes, cache hits and misses.");
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
            "Sequential — one instruction at a time. Memory hits DRAM directly every access.",
            "5-stage pipeline with stalls and squashes. Memory bypasses cache — every access hits DRAM.",
            "Sequential — one instruction at a time, with cache hierarchy.",
            "Full simulation: 5-stage pipeline + cache. Shows stalls, squashes, cache hits and misses."
        };
        modeDescLabel_->setText(descs[id]);
        currentMode_ = static_cast<SimMode>(id);

        bool usePipe  = (id == 1 || id == 3);
        bool useCache = (id == 2 || id == 3);

        pipeWidget_->setVisible(usePipe);
        seqWidget_->setVisible(!usePipe);
        cachePanel_->setVisible(useCache);
        cacheCfgBox_->setVisible(useCache);
        stepBtn_->setText(usePipe ? "Step One Cycle  ▶" : "Step One Instruction  ▶");

        simReady_ = false;
        stepBtn_->setEnabled(false);
        runBtn_->setEnabled(false);
        statusLabel_->setText("Mode changed — reload your file to apply.");
        refreshInfoBar();
    });

    // -----------------------------------------------------------------------
    // 2. Info bar — Clock Cycle and Program Counter
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
        "Total clock cycles elapsed since the simulation started."));

    QFrame* sep = new QFrame();
    sep->setFrameShape(QFrame::VLine);
    sep->setStyleSheet("color: #ccc;");
    infoHL->addWidget(sep);

    infoHL->addLayout(makeCounter("PROGRAM COUNTER (PC)", pcValueLabel_,
        "In pipeline mode: address of the next instruction to be fetched.\n"
        "In sequential mode: address of the next instruction to be executed."));

    infoHL->addStretch();
    root->addWidget(infoBar);

    // -----------------------------------------------------------------------
    // 3. Pipeline stage boxes / sequential instruction box
    // -----------------------------------------------------------------------
    pipeWidget_ = new QWidget();
    QHBoxLayout* pipeHL = new QHBoxLayout(pipeWidget_);
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

    seqWidget_ = new QWidget();
    seqWidget_->setVisible(false);
    QHBoxLayout* seqHL = new QHBoxLayout(seqWidget_);
    seqHL->setContentsMargins(0,0,0,0);
    QGroupBox* seqBox = new QGroupBox("Current Instruction");
    QVBoxLayout* seqVL = new QVBoxLayout(seqBox);
    seqInstrLabel_ = new QLabel("---");
    seqInstrLabel_->setAlignment(Qt::AlignCenter);
    QFont sf = seqInstrLabel_->font(); sf.setPointSize(11); sf.setBold(true);
    seqInstrLabel_->setFont(sf);
    seqVL->addWidget(seqInstrLabel_);
    seqHL->addWidget(seqBox, 1);

    QWidget* topArea = new QWidget();
    QVBoxLayout* topVL = new QVBoxLayout(topArea);
    topVL->setContentsMargins(0,0,0,0);
    topVL->addWidget(pipeWidget_);
    topVL->addWidget(seqWidget_);
    root->addWidget(topArea);

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
    dramNumLinesSpin_ = new QSpinBox(); dramNumLinesSpin_->setRange(4, 512); dramNumLinesSpin_->setValue(64);
    dramLineSizeSpin_ = new QSpinBox(); dramLineSizeSpin_->setRange(1, 32);  dramLineSizeSpin_->setValue(4);
    dramDelaySpin_    = new QSpinBox(); dramDelaySpin_->setRange(0, 50);     dramDelaySpin_->setValue(3);
    dramCapLabel_ = new QLabel();
    dramCapLabel_->setStyleSheet("color: #555; font-size: 9pt;");

    auto updateCap = [this]() {
        int total = dramNumLinesSpin_->value() * dramLineSizeSpin_->value();
        dramCapLabel_->setText(
            QString("Total: %1 words  (addresses 0–%2)").arg(total).arg(total - 1));
    };
    connect(dramNumLinesSpin_, QOverload<int>::of(&QSpinBox::valueChanged), [=](int){ updateCap(); });
    connect(dramLineSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), [=](int v){
        if (cacheLineSizeSpin_) cacheLineSizeSpin_->setValue(v);
        updateCap();
    });
    dramForm->addRow("Num Lines:",   dramNumLinesSpin_);
    dramForm->addRow("Line Size:",   dramLineSizeSpin_);
    dramForm->addRow("Delay (cyc):", dramDelaySpin_);
    dramForm->addRow("",             dramCapLabel_);
    updateCap();

    cacheCfgBox_ = new QGroupBox("Cache Configuration");
    QFormLayout* cacheForm = new QFormLayout(cacheCfgBox_);
    cacheNumLinesSpin_ = new QSpinBox(); cacheNumLinesSpin_->setRange(1, 64); cacheNumLinesSpin_->setValue(4);
    cacheLineSizeSpin_ = new QSpinBox(); cacheLineSizeSpin_->setRange(1, 32); cacheLineSizeSpin_->setValue(4);
    cacheLineSizeSpin_->setEnabled(false);
    cacheDelaySpin_    = new QSpinBox(); cacheDelaySpin_->setRange(0, 20);    cacheDelaySpin_->setValue(1);
    cacheForm->addRow("Num Lines:",          cacheNumLinesSpin_);
    cacheForm->addRow("Line Size (= DRAM):", cacheLineSizeSpin_);
    cacheForm->addRow("Delay (cyc):",        cacheDelaySpin_);

    QVBoxLayout* btnVL = new QVBoxLayout();
    QPushButton* loadBtn = new QPushButton("Load Instructions File…");
    loadBtn->setMinimumWidth(210);
    stepBtn_ = new QPushButton("Step One Cycle  ▶");
    stepBtn_->setMinimumWidth(210);
    stepBtn_->setEnabled(false);
    runBtn_ = new QPushButton("Run to Completion  ▶▶");
    runBtn_->setMinimumWidth(210);
    runBtn_->setEnabled(false);
    statusLabel_ = new QLabel("No program loaded.");
    statusLabel_->setStyleSheet("color: #555;");
    statusLabel_->setWordWrap(true);
    btnVL->addWidget(loadBtn);
    btnVL->addSpacing(4);
    btnVL->addWidget(stepBtn_);
    btnVL->addWidget(runBtn_);
    btnVL->addStretch();
    btnVL->addWidget(statusLabel_);

    bottomHL->addWidget(dramCfg);
    bottomHL->addWidget(cacheCfgBox_);
    bottomHL->addLayout(btnVL);
    root->addLayout(bottomHL);

    connect(loadBtn,  &QPushButton::clicked, this, &MainWindow::onLoadFile);
    connect(stepBtn_, &QPushButton::clicked, this, &MainWindow::onStep);
    connect(runBtn_,  &QPushButton::clicked, this, &MainWindow::onRunToCompletion);
}

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
                    .arg(addr).arg(norm).arg(numInstrs - 1));
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

void MainWindow::initSimulator(const std::vector<Instruction>&               program,
                               const std::vector<std::pair<int,DRAM::Line>>& dataBlocks) {
    pipeline_.reset(); seqExec_.reset(); cache_.reset(); directMem_.reset();

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

    bool usePipe  = (currentMode_ == SimMode::PIPE_ONLY  || currentMode_ == SimMode::BOTH);
    bool useCache = (currentMode_ == SimMode::CACHE_ONLY || currentMode_ == SimMode::BOTH);

    MemIF* mem = nullptr;
    if (useCache) {
        cache_ = std::make_unique<Cache>(cacheLines, lineSize, cacheDelay, dram_.get(),
            Cache::WritePolicy::WRITE_BACK, Cache::AllocatePolicy::WRITE_ALLOCATE);
        mem = cache_.get();
    } else {
        directMem_ = std::make_unique<DirectMemIF>(dram_.get());
        mem = directMem_.get();
    }

    if (usePipe)
        pipeline_ = std::make_unique<Pipeline>(PROGRAM_BASE, (int)program.size(), mem);
    else
        seqExec_  = std::make_unique<SequentialExecutor>(PROGRAM_BASE, (int)program.size(), mem);

    stepBtn_->setText(usePipe ? "Step One Cycle  ▶" : "Step One Instruction  ▶");
    simReady_ = true;
    stepBtn_->setEnabled(true);
    runBtn_->setEnabled(true);
    refreshAll();
}

void MainWindow::onStep() {
    if (!simReady_) return;
    bool usePipe = (currentMode_ == SimMode::PIPE_ONLY || currentMode_ == SimMode::BOTH);

    if (usePipe && pipeline_) {
        pipeline_->tick();
        refreshAll();
        if (pipeline_->isDone()) {
            stepBtn_->setEnabled(false); runBtn_->setEnabled(false);
            statusLabel_->setText(
                QString("Done — %1 cycles  [%2]")
                .arg(pipeline_->getCycleCount())
                .arg(QString::fromStdString(simModeName(currentMode_))));
        }
    } else if (!usePipe && seqExec_) {
        seqExec_->step();
        refreshAll();
        if (seqExec_->isDone()) {
            stepBtn_->setEnabled(false); runBtn_->setEnabled(false);
            statusLabel_->setText(
                QString("Done — %1 cycles  [%2]")
                .arg(seqExec_->getCycleCount())
                .arg(QString::fromStdString(simModeName(currentMode_))));
        }
    }
}

void MainWindow::onRunToCompletion() {
    if (!simReady_) return;
    bool usePipe = (currentMode_ == SimMode::PIPE_ONLY || currentMode_ == SimMode::BOTH);

    int cyclesBefore = usePipe
        ? (pipeline_ ? pipeline_->getCycleCount() : 0)
        : (seqExec_  ? seqExec_->getCycleCount()  : 0);

    int cyclesRun = 0;
    bool hitLimit = false;

    if (usePipe && pipeline_) {
        while (!pipeline_->isDone()) {
            pipeline_->tick();
            if (++cyclesRun > MAX_CYCLES) { hitLimit = true; break; }
        }
    } else if (!usePipe && seqExec_) {
        while (!seqExec_->isDone()) {
            seqExec_->step();
            if (++cyclesRun > MAX_CYCLES) { hitLimit = true; break; }
        }
    }

    refreshAll();

    if (hitLimit) {
        QMessageBox::warning(this, "Cycle Limit Reached",
            QString("Simulation did not complete after %1 cycles.\n\n"
                    "This may indicate an infinite loop in your program, "
                    "or a very long-running simulation.")
            .arg(MAX_CYCLES));
        statusLabel_->setText(
            QString("Stopped at cycle limit (%1 cycles)  [%2]")
            .arg(cyclesBefore + cyclesRun)
            .arg(QString::fromStdString(simModeName(currentMode_))));
        return;
    }

    stepBtn_->setEnabled(false);
    runBtn_->setEnabled(false);

    int totalCycles = usePipe ? pipeline_->getCycleCount() : seqExec_->getCycleCount();
    statusLabel_->setText(
        QString("Done — %1 cycles  [%2]")
        .arg(totalCycles)
        .arg(QString::fromStdString(simModeName(currentMode_))));
}

void MainWindow::refreshAll() {
    bool usePipe = (currentMode_ == SimMode::PIPE_ONLY || currentMode_ == SimMode::BOTH);
    if (usePipe) refreshPipelineDisplay();
    else         refreshSeqDisplay();
    refreshInfoBar();
    refreshInstructions();
    refreshRegisters();
    refreshCache();
    refreshDRAM();
}

void MainWindow::refreshInfoBar() {
    bool usePipe = (currentMode_ == SimMode::PIPE_ONLY || currentMode_ == SimMode::BOTH);
    if (!simReady_) {
        cycleValueLabel_->setText("—");
        pcValueLabel_->setText("—");
        return;
    }
    int cycle = 0, pc = 0;
    if (usePipe && pipeline_) {
        cycle = pipeline_->getCycleCount();
        pc    = pipeline_->getCurrentPC();
    } else if (!usePipe && seqExec_) {
        cycle = seqExec_->getCycleCount();
        pc    = seqExec_->getCurrentPC();
    }
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

void MainWindow::refreshSeqDisplay() {
    if (!seqExec_) return;
    seqInstrLabel_->setText(QString::fromStdString(seqExec_->getLastLabel()));
    seqInstrLabel_->setStyleSheet(
        QString("background-color: %1; border-radius: 4px; padding: 6px;").arg(C_SEQ));
}

void MainWindow::refreshInstructions() {
    instrList_->clear();
    if (program_.empty()) return;
    bool usePipe = (currentMode_ == SimMode::PIPE_ONLY || currentMode_ == SimMode::BOTH);

    auto getColor = [&](int pc) -> const char* {
        if (usePipe && pipeline_) {
            if (pc == pipeline_->getWBPC())  return C_WB;
            if (pc == pipeline_->getMEMPC()) return C_MEM;
            if (pc == pipeline_->getEXPC())  return C_EX;
            if (pc == pipeline_->getIDPC())  return C_ID;
            if (pc == pipeline_->getIFPC())  return C_IF;
        } else if (!usePipe && seqExec_) {
            if (pc == seqExec_->getLastPC()) return C_SEQ;
        }
        return nullptr;
    };

    for (int i = 0; i < (int)program_.size(); ++i) {
        int pc = PROGRAM_BASE + i;
        auto* item = new QListWidgetItem(
            QString("%1: %2").arg(i, 3).arg(QString::fromStdString(program_[i].label)));
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
    bool usePipe = (currentMode_ == SimMode::PIPE_ONLY || currentMode_ == SimMode::BOTH);
    auto getVal = [&](int i) -> int {
        if (usePipe && pipeline_) return pipeline_->readRegister(i);
        if (!usePipe && seqExec_) return seqExec_->readRegister(i);
        return 0;
    };
    for (int i = 0; i < NUM_REGS; ++i) {
        int v = getVal(i);
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
    int dataLine  = DATA_BASE / lineSize;

    for (int i = 0; i < numLines; ++i) {
        DRAM::Line line = dram_->peekLine(i * lineSize);
        QStringList words;
        for (int w : line) words << hexStr(w);
        QString tag = (i < progLines) ? " [instr]" : (i == dataLine ? " [data]" : "");
        auto* item = new QListWidgetItem(
            QString("Line %1%2 | %3").arg(i,2).arg(tag).arg(words.join(" ")));
        if (i < progLines)       item->setForeground(QColor("#6C3483"));
        else if (i == dataLine)  item->setForeground(QColor("#1A5276"));
        dramList_->addItem(item);
    }
}

void MainWindow::setStageStyle(QLabel* label, const QString& color, const QString& text) {
    label->setText(text);
    label->setStyleSheet(
        QString("background-color: %1; border-radius: 4px; padding: 4px;").arg(color));
}
