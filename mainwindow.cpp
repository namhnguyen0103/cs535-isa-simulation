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

#include <iomanip>
#include <sstream>

static const char* COLOR_IF       = "#AED6F1";
static const char* COLOR_ID       = "#A9DFBF";
static const char* COLOR_EX       = "#F9E79F";
static const char* COLOR_MEM      = "#F5CBA7";
static const char* COLOR_WB       = "#D7BDE2";
static const char* COLOR_SQUASHED = "#F1948A";
static const char* COLOR_IDLE     = "#E5E7E9";

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
    resize(1400, 820);
    setupUI();
}

void MainWindow::setupUI() {
    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout* root = new QVBoxLayout(central);
    root->setSpacing(6);
    root->setContentsMargins(8, 8, 8, 8);

    // -----------------------------------------------------------------------
    // Pipeline stage boxes
    // -----------------------------------------------------------------------
    QGroupBox* pipeBox = new QGroupBox("Pipeline Stages");
    QHBoxLayout* pipeLayout = new QHBoxLayout(pipeBox);

    auto makeStageBox = [&](const QString& title, QLabel*& lbl) {
        QGroupBox* box = new QGroupBox(title);
        box->setMinimumWidth(185);
        QVBoxLayout* vl = new QVBoxLayout(box);
        lbl = new QLabel("---");
        lbl->setWordWrap(true);
        lbl->setAlignment(Qt::AlignCenter);
        QFont f = lbl->font(); f.setPointSize(9); lbl->setFont(f);
        vl->addWidget(lbl);
        pipeLayout->addWidget(box);
    };
    makeStageBox("IF  – Fetch",      stageIF_);
    makeStageBox("ID  – Decode",     stageID_);
    makeStageBox("EX  – Execute",    stageEX_);
    makeStageBox("MEM – Memory",     stageMEM_);
    makeStageBox("WB  – Write Back", stageWB_);

    cycleLabel_ = new QLabel("Cycle: 0");
    QFont cf = cycleLabel_->font(); cf.setBold(true); cf.setPointSize(12);
    cycleLabel_->setFont(cf);
    cycleLabel_->setAlignment(Qt::AlignCenter);
    cycleLabel_->setMinimumWidth(90);
    pipeLayout->addStretch();
    pipeLayout->addWidget(cycleLabel_);
    root->addWidget(pipeBox);

    // -----------------------------------------------------------------------
    // Data panels
    // -----------------------------------------------------------------------
    QHBoxLayout* panelsLayout = new QHBoxLayout();
    panelsLayout->setSpacing(6);
    auto makePanel = [&](const QString& title, QListWidget*& list) {
        QGroupBox* box = new QGroupBox(title);
        QVBoxLayout* vl = new QVBoxLayout(box);
        list = new QListWidget();
        list->setFont(QFont("Courier", 9));
        list->setSelectionMode(QAbstractItemView::NoSelection);
        vl->addWidget(list);
        panelsLayout->addWidget(box);
    };
    makePanel("Instructions", instrList_);
    makePanel("Registers",    regList_);
    makePanel("Cache",        cacheList_);
    makePanel("DRAM",         dramList_);
    root->addLayout(panelsLayout, 1);

    // -----------------------------------------------------------------------
    // Bottom: memory config + controls
    // -----------------------------------------------------------------------
    QHBoxLayout* bottomLayout = new QHBoxLayout();
    bottomLayout->setSpacing(10);

    // DRAM config
    QGroupBox* dramCfg = new QGroupBox("DRAM Configuration");
    QFormLayout* dramForm = new QFormLayout(dramCfg);
    dramNumLinesSpin_ = new QSpinBox(); dramNumLinesSpin_->setRange(4, 512); dramNumLinesSpin_->setValue(32);
    dramLineSizeSpin_ = new QSpinBox(); dramLineSizeSpin_->setRange(1, 32);  dramLineSizeSpin_->setValue(4);
    dramDelaySpin_    = new QSpinBox(); dramDelaySpin_->setRange(0, 50);     dramDelaySpin_->setValue(3);

    QLabel* dramCapLabel = new QLabel();
    auto updateCapLabel = [=]() {
        int total = dramNumLinesSpin_->value() * dramLineSizeSpin_->value();
        dramCapLabel->setText(QString("Total words: %1  (max address: %2)")
            .arg(total).arg(total - 1));
        dramCapLabel->setStyleSheet("color: #555; font-size: 9pt;");
    };
    connect(dramNumLinesSpin_, QOverload<int>::of(&QSpinBox::valueChanged), [=](int){ updateCapLabel(); });
    connect(dramLineSizeSpin_, QOverload<int>::of(&QSpinBox::valueChanged), [=](int v){
        if (cacheLineSizeSpin_) cacheLineSizeSpin_->setValue(v);
        updateCapLabel();
    });
    updateCapLabel();

    dramForm->addRow("Num Lines:",    dramNumLinesSpin_);
    dramForm->addRow("Line Size:",    dramLineSizeSpin_);
    dramForm->addRow("Delay (cyc):",  dramDelaySpin_);
    dramForm->addRow("",              dramCapLabel);

    // Cache config
    QGroupBox* cacheCfg = new QGroupBox("Cache Configuration");
    QFormLayout* cacheForm = new QFormLayout(cacheCfg);
    cacheNumLinesSpin_ = new QSpinBox(); cacheNumLinesSpin_->setRange(1, 64); cacheNumLinesSpin_->setValue(4);
    cacheLineSizeSpin_ = new QSpinBox(); cacheLineSizeSpin_->setRange(1, 32); cacheLineSizeSpin_->setValue(4);
    cacheLineSizeSpin_->setEnabled(false);
    cacheDelaySpin_    = new QSpinBox(); cacheDelaySpin_->setRange(0, 20);    cacheDelaySpin_->setValue(1);

    cacheForm->addRow("Num Lines:",          cacheNumLinesSpin_);
    cacheForm->addRow("Line Size (= DRAM):", cacheLineSizeSpin_);
    cacheForm->addRow("Delay (cyc):",        cacheDelaySpin_);

    // Buttons
    QVBoxLayout* btnLayout = new QVBoxLayout();
    QPushButton* loadBtn = new QPushButton("Load Instructions File…");
    loadBtn->setMinimumWidth(200);
    stepBtn_ = new QPushButton("Step One Cycle  ▶");
    stepBtn_->setMinimumWidth(180);
    stepBtn_->setEnabled(false);
    statusLabel_ = new QLabel("No program loaded.");
    statusLabel_->setStyleSheet("color: #555;");
    statusLabel_->setWordWrap(true);
    btnLayout->addWidget(loadBtn);
    btnLayout->addWidget(stepBtn_);
    btnLayout->addStretch();
    btnLayout->addWidget(statusLabel_);

    bottomLayout->addWidget(dramCfg);
    bottomLayout->addWidget(cacheCfg);
    bottomLayout->addLayout(btnLayout);
    root->addLayout(bottomLayout);

    connect(loadBtn,  &QPushButton::clicked, this, &MainWindow::onLoadFile);
    connect(stepBtn_, &QPushButton::clicked, this, &MainWindow::onStep);

    for (auto* l : {stageIF_, stageID_, stageEX_, stageMEM_, stageWB_})
        setStageStyle(l, COLOR_IDLE, "---");
}

void MainWindow::onLoadFile() {
    QString path = QFileDialog::getOpenFileName(
        this, "Open Instructions File", "", "Text Files (*.txt);;All Files (*)");
    if (path.isEmpty()) return;

    try {
        ParseResult parsed = parseInstructionFile(path.toStdString());
        if (parsed.program.empty()) {
            QMessageBox::warning(this, "Empty Program", "No instructions found.");
            return;
        }

        // Warn if any DATA address might collide with instructions
        int numInstrs = static_cast<int>(parsed.program.size());
        for (auto& [addr, line] : parsed.dataBlocks) {
            int totalDramWords = dramNumLinesSpin_->value() * dramLineSizeSpin_->value();
            int normalized = addr % totalDramWords;
            if (normalized < numInstrs) {
                QMessageBox::warning(this, "Address Collision Warning",
                    QString("DATA at address %1 (normalizes to %2) overlaps with "
                            "instruction area (addresses 0–%3).\n\n"
                            "Increase your DRAM size or use a higher DATA address.")
                    .arg(addr).arg(normalized).arg(numInstrs - 1));
            }
        }

        program_ = parsed.program;
        initSimulator(parsed.program, parsed.dataBlocks);
        statusLabel_->setText("Loaded: " + QFileInfo(path).fileName());
    } catch (const std::exception& ex) {
        QMessageBox::critical(this, "Parse Error", QString::fromStdString(ex.what()));
    }
}

void MainWindow::initSimulator(const std::vector<Instruction>&               program,
                               const std::vector<std::pair<int,DRAM::Line>>& dataBlocks) {
    int dramLines  = dramNumLinesSpin_->value();
    int lineSize   = dramLineSizeSpin_->value();
    int dramDelay  = dramDelaySpin_->value();
    int cacheLines = cacheNumLinesSpin_->value();
    int cacheDelay = cacheDelaySpin_->value();

    dram_ = std::make_unique<DRAM>(dramLines, lineSize, dramDelay);
    cache_ = std::make_unique<Cache>(
        cacheLines, lineSize, cacheDelay, dram_.get(),
        Cache::WritePolicy::WRITE_BACK,
        Cache::AllocatePolicy::WRITE_ALLOCATE);

    // Write encoded instructions first
    loadProgramToDRAM(program, *dram_, PROGRAM_BASE);

    // Write DATA blocks after — they go to their specified addresses
    for (auto& [addr, line] : dataBlocks) {
        DRAM::Line padded(lineSize, 0);
        for (int i = 0; i < std::min((int)line.size(), lineSize); ++i)
            padded[i] = line[i];
        dram_->setLineDirect(addr, padded);
    }

    pipeline_ = std::make_unique<Pipeline>(
        PROGRAM_BASE, static_cast<int>(program.size()), cache_.get());

    simReady_ = true;
    stepBtn_->setEnabled(true);
    cycleLabel_->setText("Cycle: 0");
    refreshAll();
}

void MainWindow::onStep() {
    if (!simReady_ || !pipeline_) return;
    pipeline_->tick();
    cycleLabel_->setText("Cycle: " + QString::number(pipeline_->getCycleCount()));
    refreshAll();
    if (pipeline_->isDone()) {
        stepBtn_->setEnabled(false);
        statusLabel_->setText("Done — " +
            QString::number(pipeline_->getCycleCount()) + " cycles.");
    }
}

void MainWindow::refreshAll() {
    refreshPipelineDisplay();
    refreshInstructions();
    refreshRegisters();
    refreshCache();
    refreshDRAM();
}

void MainWindow::refreshPipelineDisplay() {
    if (!pipeline_) return;
    auto stageColor = [](const std::string& lbl, const char* normal) -> const char* {
        if (lbl.find("[squashed]") != std::string::npos) return COLOR_SQUASHED;
        if (lbl == "---") return COLOR_IDLE;
        return normal;
    };
    auto q = [](const std::string& s) { return QString::fromStdString(s); };
    setStageStyle(stageIF_,  stageColor(pipeline_->getIFLabel(),  COLOR_IF),  q(pipeline_->getIFLabel()));
    setStageStyle(stageID_,  stageColor(pipeline_->getIDLabel(),  COLOR_ID),  q(pipeline_->getIDLabel()));
    setStageStyle(stageEX_,  stageColor(pipeline_->getEXLabel(),  COLOR_EX),  q(pipeline_->getEXLabel()));
    setStageStyle(stageMEM_, stageColor(pipeline_->getMEMLabel(), COLOR_MEM), q(pipeline_->getMEMLabel()));
    setStageStyle(stageWB_,  stageColor(pipeline_->getWBLabel(),  COLOR_WB),  q(pipeline_->getWBLabel()));
}

void MainWindow::refreshInstructions() {
    instrList_->clear();
    if (program_.empty()) return;
    int base = pipeline_ ? pipeline_->getProgramBase() : PROGRAM_BASE;
    auto pcColor = [&](int pc) -> const char* {
        if (!pipeline_) return nullptr;
        if (pc == pipeline_->getWBPC())  return COLOR_WB;
        if (pc == pipeline_->getMEMPC()) return COLOR_MEM;
        if (pc == pipeline_->getEXPC())  return COLOR_EX;
        if (pc == pipeline_->getIDPC())  return COLOR_ID;
        if (pc == pipeline_->getIFPC())  return COLOR_IF;
        return nullptr;
    };
    for (int i = 0; i < (int)program_.size(); ++i) {
        int pc = base + i;
        auto* item = new QListWidgetItem(
            QString("%1: %2").arg(i, 3).arg(QString::fromStdString(program_[i].label)));
        const char* color = pcColor(pc);
        if (color) {
            item->setBackground(QColor(color));
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
        QString line;
        if (i == REG_FLAGS) {
            line = QString("%1= %2  flags: %3")
                .arg(registerName(i)).arg(hexStr(v)).arg(flagsDescription(v));
        } else {
            line = QString("%1= %2  (%3)")
                .arg(registerName(i)).arg(v).arg(hexStr(v));
        }
        auto* item = new QListWidgetItem(line);
        if (i == REG_ZERO || i == REG_RA || i == REG_SP || i == REG_FLAGS) {
            item->setForeground(QColor("#1A5276"));
            QFont f = regList_->font(); f.setBold(true); item->setFont(f);
        } else if (v != 0) {
            item->setForeground(Qt::darkBlue);
        } else {
            item->setForeground(Qt::gray);
        }
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
        else
            item->setForeground(Qt::gray);
        cacheList_->addItem(item);
    }
}

void MainWindow::refreshDRAM() {
    dramList_->clear();
    if (!dram_) return;
    int numLines  = dram_->getNumLines();
    int lineSize  = dram_->getLineSize();
    int progLines = ((int)program_.size() + lineSize - 1) / lineSize;
    // Data lines: any line whose base address matches a DATA block
    int dataStartLine = DATA_BASE / lineSize;

    for (int i = 0; i < numLines; ++i) {
        DRAM::Line line = dram_->peekLine(i * lineSize);
        QStringList words;
        for (int w : line) words << hexStr(w);

        QString tag;
        if (i < progLines)          tag = " [instr]";
        else if (i == dataStartLine) tag = " [data]";

        auto* item = new QListWidgetItem(
            QString("Line %1%2 | %3").arg(i,2).arg(tag).arg(words.join(" ")));

        if (i < progLines)           item->setForeground(QColor("#6C3483"));
        else if (i == dataStartLine) item->setForeground(QColor("#1A5276"));

        dramList_->addItem(item);
    }
}

void MainWindow::setStageStyle(QLabel* label, const QString& color, const QString& text) {
    label->setText(text);
    label->setStyleSheet(
        QString("background-color: %1; border-radius: 4px; padding: 4px;").arg(color));
}
