#!/usr/bin/env python3
"""
DPDK Log to Corporate PDF Converter (MULTIPROCESSING TURBO EDITION)
Parses DPDK test logs and generates a highly formatted corporate PDF.
Strict 3-Page per test layout + Test Summary Page.
Drops the first test from PDF and subtracts 1 second from phase names.
"""

import os
import sys
import re
import argparse
import multiprocessing as mp
from datetime import datetime
from typing import List, Dict

try:
    from reportlab.lib import colors
    from reportlab.lib.pagesizes import letter
    from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
    from reportlab.lib.units import inch
    from reportlab.platypus import (SimpleDocTemplate, Paragraph, Spacer, 
                                    PageBreak, Image, HRFlowable, Table, TableStyle, Flowable)
    from reportlab.pdfbase.pdfmetrics import stringWidth
    from reportlab.lib.enums import TA_CENTER, TA_LEFT
except ImportError:
    print("ERROR: reportlab is not installed. Please install it with: pip install reportlab")
    sys.exit(1)

# ==========================================
# DEFAULTS
# ==========================================
DEFAULT_LOGO_PATH = "assets/company_logo.png"
DEFAULT_DEVICE_MODEL = "N/A"
DEFAULT_DEVICE_SERIAL = "N/A"
DEFAULT_TESTER_NAME = "N/A"
DEFAULT_QUALITY_CHECKER = "N/A"
DEFAULT_REVISION_DATE = "19/02/2026"
DEFAULT_TEST_DATE = "N/A"

RE_HEALTH_BLOCK = re.compile(r'\[HEALTH\]\s+(\d+)\s+\|\s+\d+\s+\|\s+\d+\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|\s+(\d+)\s+\|')
RE_DURATION = re.compile(r'(\d+)\s*(sn|sec)')
RE_MAIN_TABLE = re.compile(r'^\d+\s*║')
RE_RAW_TABLE = re.compile(r'^P\d+\s*║')

# ==========================================
# TIME CONVERTER
# ==========================================
def format_duration(duration_str: str) -> str:
    """ 7129s formatındaki süreyi '7129s (01:58:49)' formatına çevirir """
    try:
        match = re.search(r'\d+', duration_str)
        if match:
            seconds = int(match.group())
            h = seconds // 3600
            m = (seconds % 3600) // 60
            s = seconds % 60
            return f"{seconds}s ({h:02d}:{m:02d}:{s:02d})"
    except Exception:
        pass
    return duration_str

# ==========================================
# ARRANGE THE WORDS
# ==========================================
STRING_WIDTH_CACHE = {}

class ShrinkToFit(Flowable):
    def __init__(self, text, font_name='Helvetica', font_size=7, text_color=colors.black):
        Flowable.__init__(self)
        self.text = str(text)
        self.font_name = font_name
        self.font_size = font_size
        self.text_color = text_color
        
    def wrap(self, availWidth, availHeight):
        self.availWidth = availWidth
        cache_key = (self.text, self.font_name, self.font_size)
        if cache_key not in STRING_WIDTH_CACHE:
            STRING_WIDTH_CACHE[cache_key] = stringWidth(self.text, self.font_name, self.font_size)
            
        width = STRING_WIDTH_CACHE[cache_key]
        self.scale = 1.0
        if width > availWidth and width > 0:
            self.scale = (availWidth - 2) / width
        
        self.draw_font_size = self.font_size * self.scale
        self.width = availWidth
        self.height = self.font_size * 1.0 
        return self.width, self.height

    def draw(self):
        self.canv.saveState()
        self.canv.setFillColor(self.text_color)
        self.canv.setFont(self.font_name, self.draw_font_size)
        self.canv.drawCentredString(self.width / 2.0, 1, self.text)
        self.canv.restoreState()


# ==========================================
# 1. PARSER
# ==========================================
class LogParser:
    def __init__(self, filepath: str):
        self.filepath = filepath
        self.data = {
            "metadata": {},
            "phases": [],
            "test_duration": "N/A",
            "first_assistant_block": [],
            "first_manager_block": [],
            "last_assistant_block": [],
            "last_manager_block": [],
            "mismatches": [],
            "reference_phase_name": "N/A"
        }
        self.first_health = {}
        self.last_health = {} 

    def parse(self):
        with open(self.filepath, 'r', encoding='utf-8', errors='ignore') as f:
            state = "NORMAL"
            current_phase = None
            health_capture_state = None
            health_table_state = None
            
            current_assistant_block = []
            current_manager_block = []
            
            meta_keys = ["Date/Time", "Test Name", "Serial Number", "Tester Name", "Quality Checker", "Unit Name", "Revision Date", "Revision"]

            for line in f:
                line_clean = line.strip()
                if not line_clean:
                    continue

                # --- METADATA ---
                is_meta_line = False
                for mk in meta_keys:
                    if line_clean.startswith(mk):
                        parts = line_clean.split(":", 1)
                        if len(parts) == 2:
                            self.data["metadata"][parts[0].strip()] = parts[1].strip()
                            is_meta_line = True
                            break
                if is_meta_line: continue

                # --- HEALTH BLOK & PDF İÇİN DATA YAKALAMA ---
                if "[HEALTH]" in line_clean:
                    h_line = line_clean.replace("[HEALTH]", "").strip()
                    
                    if h_line.startswith("============ ASSISTANT FPGA"):
                        health_capture_state = "ASSISTANT"
                        health_table_state = "AST_META"
                        current_assistant_block = [line_clean]
                    elif h_line.startswith("---- ASSISTANT FPGA Port Status"):
                        health_table_state = "AST_TABLE"
                    elif h_line.startswith("============ MANAGER FPGA"):
                        health_capture_state = "MANAGER"
                        health_table_state = "MGR_META"
                        current_manager_block = [line_clean]
                    elif h_line.startswith("---- MANAGER FPGA Port Status"):
                        health_table_state = "MGR_TABLE"
                    elif h_line.startswith("================================================"):
                        if health_capture_state == "ASSISTANT":
                            current_assistant_block.append(line_clean)
                            self.data["last_assistant_block"] = current_assistant_block
                            if not self.data["first_assistant_block"]:
                                self.data["first_assistant_block"] = current_assistant_block
                        elif health_capture_state == "MANAGER":
                            current_manager_block.append(line_clean)
                            self.data["last_manager_block"] = current_manager_block
                            if not self.data["first_manager_block"]:
                                self.data["first_manager_block"] = current_manager_block
                        health_capture_state = None
                        health_table_state = None
                    else:
                        if health_capture_state == "ASSISTANT": current_assistant_block.append(line_clean)
                        elif health_capture_state == "MANAGER": current_manager_block.append(line_clean)

                        if current_phase is not None:
                            if health_table_state == "AST_META":
                                current_phase["ast_meta"].append(h_line)
                            elif health_table_state == "MGR_META":
                                current_phase["mgr_meta"].append(h_line)
                            elif health_table_state == "AST_TABLE":
                                if not h_line.startswith("Port |") and not h_line.startswith("-----|"):
                                    parts = [p.strip() for p in h_line.split('|') if p.strip()]
                                    if len(parts) >= 8: current_phase["ast_table"].append(parts)
                            elif health_table_state == "MGR_TABLE":
                                if not h_line.startswith("Port |") and not h_line.startswith("-----|"):
                                    parts = [p.strip() for p in h_line.split('|') if p.strip()]
                                    if len(parts) >= 8: current_phase["mgr_table"].append(parts)

                    match = RE_HEALTH_BLOCK.search(line_clean)
                    if match:
                        port = int(match.group(1))
                        drops = sum(int(match.group(i)) for i in range(2, 7))
                        if port not in self.first_health:
                            self.first_health[port] = drops
                        self.last_health[port] = drops
                        if current_phase is not None:
                            current_phase["has_health"] = True
                    continue

                if line_clean.startswith("========== [TEST"):
                    if current_phase: self.data["phases"].append(current_phase)
                    
                    match = RE_DURATION.search(line_clean)
                    if match: self.data["test_duration"] = match.group(1) + "s"

                    current_phase = {
                        "name": line_clean.strip("= []").replace("sn", "sec"),
                        "has_health": False, 
                        "main_table": [],
                        "raw_multi_table": [],
                        "port12_table": [],
                        "port13_table": [],
                        "ast_meta": [],
                        "ast_table": [],
                        "mgr_meta": [],
                        "mgr_table": []
                    }
                    state = "PHASE_MAIN_TABLE"
                    continue
                
                if line_clean.startswith("========== [WARM-UP"):
                    if current_phase: self.data["phases"].append(current_phase)
                    current_phase = None
                    state = "NORMAL"
                    continue

                # --- TABLOLARI OKUMA ---
                if current_phase and state.startswith("PHASE_"):
                    clean_table_line = line_clean.replace("│", "║").strip("║ ")
                    if "║" in clean_table_line:
                        if RE_MAIN_TABLE.match(clean_table_line):
                            parts = [p.strip() for p in clean_table_line.split("║")]
                            if len(parts) >= 12: current_phase["main_table"].append(parts)
                        elif RE_RAW_TABLE.match(clean_table_line):
                            parts = [p.strip() for p in clean_table_line.split("║")]
                            if len(parts) >= 11: current_phase["raw_multi_table"].append(parts)
                        elif state == "PHASE_PORT12_TABLE" and not "RX Pkts" in line_clean:
                            parts = [p.strip() for p in clean_table_line.split("║")]
                            if len(parts) >= 6: current_phase["port12_table"].append(parts)
                            state = "PHASE_MAIN_TABLE"
                        elif state == "PHASE_PORT13_TABLE" and not "RX Pkts" in line_clean:
                            parts = [p.strip() for p in clean_table_line.split("║")]
                            if len(parts) >= 6: current_phase["port13_table"].append(parts)
                            state = "PHASE_MAIN_TABLE"
                    else:
                        if "Port 12 RX" in line_clean: state = "PHASE_PORT12_TABLE"
                        elif "Port 13 RX" in line_clean: state = "PHASE_PORT13_TABLE"

            if current_phase:
                self.data["phases"].append(current_phase)

        # 1. HESAPLAMA YAP (Arka planda tüm veriler kullanılarak yapılır)
        self._evaluate_test_results()
        
        # 2. SADECE HEALTH DATASI OLAN GEÇERLİ TESTLERİ TUT
        valid_phases = [p for p in self.data["phases"] if p.get("has_health")]
        
        # 3. İLK TESTİ PDF'TEN ÇIKAR VE KALANLARIN BAŞLIĞINDAKİ SÜREYİ 1 SANİYE AZALT
        if len(valid_phases) > 0:
            valid_phases = valid_phases[1:] # İlk testi (indeks 0) çöpe atar
            
            for phase in valid_phases:
                # Başlıktaki (örn: TEST 2 sec) sayıyı bulup 1 eksiltiriz
                phase["name"] = re.sub(r'\d+', lambda m: str(max(0, int(m.group(0)) - 1)), phase["name"])
                
        self.data["phases"] = valid_phases
        
        # Global test süresini de 1 saniye azaltalım ki kapakla uyumlu olsun
        if self.data["test_duration"] != "N/A":
            try:
                dur_int = int(re.search(r'\d+', self.data["test_duration"]).group(0))
                if dur_int > 0:
                    self.data["test_duration"] = f"{dur_int - 1}s"
            except Exception:
                pass
        
        return self.data

    def _evaluate_test_results(self):
        global_fail = False
        mismatches = []
        target_phase = None
        for phase in reversed(self.data["phases"]):
            if phase.get("main_table") and phase.get("has_health"):
                target_phase = phase
                break

        if target_phase:
            self.data["reference_phase_name"] = target_phase["name"]
            for row in target_phase["main_table"]:
                if len(row) > 9 and row[0].isdigit():
                    try:
                        port = int(row[0])
                        table_lost = int(row[9].replace(',', '').strip())
                        baseline = self.first_health.get(port, 0)
                        final = self.last_health.get(port, 0)
                        real_drops = final - baseline
                        
                        if real_drops != table_lost:
                            global_fail = True
                            mismatches.append(f"Port {port} Uyusmazligi -> Tabloda: {table_lost} | Cihazda: {real_drops}")
                    except ValueError:
                        pass

        self.data["metadata"]["Test Result"] = "Fail" if global_fail else "Pass"
        self.data["mismatches"] = mismatches 


# ==========================================
# 2. PDF TEMPLATE AND GENERATION
# ==========================================
class PDFReportTemplate:
    def __init__(self, logo_path: str = None):
        self.logo_path = logo_path
        self.styles = getSampleStyleSheet()
        self._setup_custom_styles()

        self.document_name = "TEST REPORT"
        self.document_number = "IPPP-HW-#####"
        self.test_result = "Pass"
        self.revision = "0.0A"
        self.revision_date = DEFAULT_REVISION_DATE 
        self.report_date = datetime.now().strftime('%d.%m.%Y')
        self.test_date = DEFAULT_TEST_DATE
        self.tester_name = DEFAULT_TESTER_NAME
        self.quality_checker_name = DEFAULT_QUALITY_CHECKER
        self.device_model = DEFAULT_DEVICE_MODEL
        self.device_serial = DEFAULT_DEVICE_SERIAL
        self.chunk_info = ""

    def apply_metadata(self, meta: Dict[str, str]):
        if "Test Name" in meta: self.document_name = meta["Test Name"]
        if "Serial Number" in meta: self.device_serial = meta["Serial Number"]
        if "Tester Name" in meta: self.tester_name = meta["Tester Name"]
        if "Quality Checker" in meta: self.quality_checker_name = meta["Quality Checker"]
        if "Unit Name" in meta: self.device_model = meta["Unit Name"]
        if "Date/Time" in meta: self.test_date = meta["Date/Time"]
        if "Revision Date" in meta: self.revision_date = meta["Revision Date"]
        if "Revision" in meta: self.revision = meta["Revision"]
        if "Test Result" in meta: self.test_result = meta["Test Result"]

    def _setup_custom_styles(self):
        self.styles.add(ParagraphStyle(name='CustomTitle', parent=self.styles['Heading1'], fontSize=24,
            textColor=colors.HexColor('#1a5490'), spaceAfter=30, alignment=TA_CENTER, fontName='Helvetica-Bold', leading=28))
        self.styles.add(ParagraphStyle(name='SectionHeader', parent=self.styles['Heading2'], fontSize=16,
            textColor=colors.HexColor('#1a5490'), spaceAfter=20, spaceBefore=24, fontName='Helvetica-Bold', leading=20))
        self.styles.add(ParagraphStyle(name='EnhancedBody', parent=self.styles['Normal'], fontSize=10, leading=14,
            spaceAfter=8, alignment=TA_LEFT, fontName='Helvetica'))
        self.styles.add(ParagraphStyle(name='PhaseTitle', fontSize=12, textColor=colors.white, backColor=colors.HexColor('#e67e22'), 
            spaceAfter=10, spaceBefore=15, fontName='Helvetica-Bold', alignment=TA_CENTER, borderPadding=5))
        self.styles.add(ParagraphStyle(name='SubTitle', fontSize=10, fontName='Helvetica-Bold', spaceAfter=6, spaceBefore=10))
        self.styles.add(ParagraphStyle(name='HealthMeta', parent=self.styles['Normal'], fontSize=9, leading=16,
            textColor=colors.HexColor('#222222'), spaceAfter=15, alignment=TA_CENTER))

    def _create_header_footer(self, canvas_obj, doc, is_cover_page=False):
        canvas_obj.saveState()
        page_width, page_height = letter

        if self.logo_path and os.path.exists(self.logo_path):
            try:
                header_logo_size = 50
                canvas_obj.drawImage(self.logo_path, 0.5 * inch, page_height - 1.07 * inch,
                    width=header_logo_size, height=header_logo_size, preserveAspectRatio=True, mask='auto')
            except Exception: pass

        if not is_cover_page:
            canvas_obj.setFont('Helvetica-Bold', 10)
            canvas_obj.setFillColor(colors.HexColor('#DD0000'))
            canvas_obj.drawCentredString(page_width / 2, page_height - 0.65 * inch, "RESTRICTED")

        canvas_obj.setFont('Helvetica-Bold', 10)
        canvas_obj.setFillColor(colors.HexColor('#2c5aa0'))
        canvas_obj.drawCentredString(page_width / 2, page_height - 0.85 * inch, "INTEGRATED PROCESSING POOL PLATFORM PROJECT")

        if doc.page > 1:
            canvas_obj.setFont('Helvetica', 8)
            canvas_obj.setFillColor(colors.HexColor('#555555'))
            canvas_obj.drawCentredString(page_width / 2, page_height - 0.98 * inch, self.document_name[:60])

        canvas_obj.setStrokeColor(colors.HexColor('#2c5aa0'))
        canvas_obj.setLineWidth(1.5)
        canvas_obj.line(0.5 * inch, page_height - 1.10 * inch, page_width - 0.5 * inch, page_height - 1.10 * inch)

        canvas_obj.setFillColor(colors.HexColor('#f0f0f0'))
        canvas_obj.rect(0.5 * inch, 0.85 * inch, page_width - 1.0 * inch, 0.25 * inch, fill=1, stroke=0)
        canvas_obj.setStrokeColor(colors.HexColor('#2c5aa0'))
        canvas_obj.setLineWidth(1)
        canvas_obj.rect(0.5 * inch, 0.85 * inch, page_width - 1.0 * inch, 0.25 * inch, fill=0, stroke=1)

        canvas_obj.setFont('Helvetica', 8)
        canvas_obj.setFillColor(colors.HexColor('#1a1a1a'))
        canvas_obj.drawString(0.6 * inch, 0.93 * inch, self.document_number)

        separator1_x = 0.5 * inch + (page_width - 1.0 * inch) / 3
        separator2_x = 0.5 * inch + 2 * (page_width - 1.0 * inch) / 3
        canvas_obj.line(separator1_x, 0.85 * inch, separator1_x, 1.10 * inch)
        canvas_obj.line(separator2_x, 0.85 * inch, separator2_x, 1.10 * inch)

        canvas_obj.drawCentredString((separator1_x + separator2_x) / 2, 0.93 * inch, f"Revision Date: {self.revision_date}")
        
        page_text = f"Report Date: {self.report_date} {self.chunk_info}" if is_cover_page else f"Report Date: {self.report_date} | Page {doc.page} {self.chunk_info}"
        canvas_obj.drawCentredString((separator2_x + (page_width - 0.5 * inch)) / 2, 0.93 * inch, page_text)

        canvas_obj.setFont('Helvetica-Bold', 10)
        canvas_obj.setFillColor(colors.HexColor('#DD0000'))
        canvas_obj.drawCentredString(page_width / 2, 0.55 * inch, "RESTRICTED")

        if doc.page > 1:
            canvas_obj.setFont('Helvetica', 6)
            canvas_obj.setFillColor(colors.HexColor('#666666'))
            x_pos = 0.25 * inch
            canvas_obj.saveState()
            canvas_obj.translate(x_pos, page_height - 3.37 * inch)
            canvas_obj.rotate(90)
            canvas_obj.drawString(0, 0, "The contents of this document are the property of TUBITAK BILGEM")
            canvas_obj.drawString(0, -0.10 * inch, "and should not be reproduced, copied or disclosed to a third party")
            canvas_obj.drawString(0, -0.20 * inch, "without the written consent of the proprietor.")
            canvas_obj.restoreState()

            canvas_obj.saveState()
            canvas_obj.translate(x_pos, page_height / 2 - 1.07 * inch)
            canvas_obj.rotate(90)
            canvas_obj.drawString(0, 0, "© TUBITAK BILGEM - Bilisim ve Bilgi Guvenligi Ileri Teknolojiler Arastirma Merkezi")
            canvas_obj.drawString(0, -0.10 * inch, "P.K 74, Gebze, 41470 Kocaeli, Turkiye")
            canvas_obj.drawString(0, -0.20 * inch, "Tel: (0262) 675 30 00, Fax: (0262) 648 11 00")
            canvas_obj.restoreState()

            canvas_obj.saveState()
            canvas_obj.translate(x_pos, 1.23 * inch)
            canvas_obj.rotate(90)
            canvas_obj.drawString(0, 0, "Bu dokumanin icerigi Tubitak Bilgem mulkiyetindedir.")
            canvas_obj.drawString(0, -0.10 * inch, "Sahibinin yazili izni olmadan cogaltilamaz, kopyalanamaz ve")
            canvas_obj.drawString(0, -0.20 * inch, "ucuncu sahislara aciklanamaz.")
            canvas_obj.restoreState()

        canvas_obj.restoreState()

    def _draw_cover_page_border(self, canvas_obj, doc):
        canvas_obj.saveState()
        border_color = colors.HexColor('#2c5aa0')
        margin = 0.40 * inch 
        canvas_obj.setStrokeColor(border_color)
        canvas_obj.setLineWidth(4)
        canvas_obj.setLineCap(1)
        canvas_obj.setLineJoin(1)
        canvas_obj.rect(margin, margin, letter[0] - 2 * margin, letter[1] - 2 * margin, stroke=1, fill=0)
        canvas_obj.restoreState()

    def _create_cover_page(self) -> List:
        elements = [Spacer(1, 0.3 * inch)]

        if self.logo_path and os.path.exists(self.logo_path):
            try:
                cover_logo_size = 160 
                logo_img = Image(self.logo_path, width=cover_logo_size, height=cover_logo_size, kind='proportional')
                logo_img.hAlign = 'CENTER'
                elements.append(logo_img)
            except Exception: pass

        elements.append(Spacer(1, 0.2 * inch))
        elements.append(HRFlowable(width="80%", thickness=2, color=colors.HexColor('#2c5aa0'), spaceAfter=0.25*inch, hAlign='CENTER'))
        elements.append(Paragraph('<para alignment="center" fontSize="14" textColor="#2c5aa0"><b>INFORMATICS AND INFORMATION SECURITY RESEARCH CENTER</b></para>', self.styles['Normal']))
        elements.append(Spacer(1, 0.35 * inch))
        elements.append(Paragraph('<para alignment="center" fontSize="12" textColor="#1a1a1a"><b>INTEGRATED PROCESSING POOL PLATFORM PROJECT</b></para>', self.styles['Normal']))
        elements.append(Spacer(1, 0.35 * inch))
        
        title_text = f"""
        <para alignment="center" fontSize="16" textColor="#1a1a1a" leading="18">
        <b>{self.device_model}</b><br/>
        <font size="14"><b>{self.document_name}</b></font><br/>
        <font size="14"><b>TEST RESULT</b></font>
        </para>
        """
        elements.append(Paragraph(title_text, self.styles['Normal']))
        elements.append(Spacer(1, 0.3 * inch))

        status_color = "#00AA00" if self.test_result.upper() == "PASS" else "#DD0000"

        doc_info = f"""
        <para alignment="center" fontSize="10" textColor="#1a1a1a">
        <b>Document Number:</b> {self.document_number}<br/>
        <b>Test Result:</b> <font color='{status_color}'><b>{self.test_result}</b></font><br/>
        <b>Revision:</b> {self.revision}<br/>
        <b>Revision Date:</b> {self.revision_date}<br/>
        <b>Report Date:</b> {self.report_date}<br/>
        <b>Tester:</b> {self.tester_name}<br/>
        <b>Quality Checker:</b> {self.quality_checker_name}
        </para>
        """
        elements.append(Paragraph(doc_info, self.styles['Normal']))
        elements.append(Spacer(1, 0.35 * inch))

        elements.append(HRFlowable(width="80%", thickness=2, color=colors.HexColor('#2c5aa0'), spaceAfter=0.15*inch, hAlign='CENTER'))
        
        footer_text = """
        <para alignment="center" fontSize="8" textColor="#1a1a1a">
        <b>@TUBITAK BILGEM</b><br/>
        Informatics and Information Security Research Center<br/>
        P.C.74, 41470 GEBZE, KOCAELI, TURKIYE<br/>
        Tel : +90 (262) 675 30 00,  Fax: +90 (262) 648 11 00<br/>
        www.bilgem.tubitak.gov.tr | bilgem@tubitak.gov.tr
        </para>
        """
        elements.append(Paragraph(footer_text, self.styles['Normal']))
        elements.append(PageBreak())
        return elements

    def _create_product_info_section(self, test_duration: str) -> List:
        elements = []
        elements.append(Paragraph("Product Information", self.styles['SectionHeader']))

        table_data = [
            ["Test Duration:", test_duration],
            ["Device Model:", self.device_model],
            ["Test Date:", self.test_date],
            ["Tester:", self.tester_name],
            ["Quality Checker:", self.quality_checker_name],
            ["Device Serial:", self.device_serial]
        ]
        
        t = Table(table_data, colWidths=[2.0*inch, 4.5*inch])
        t.setStyle(TableStyle([
            ('TEXTCOLOR', (0, 0), (-1, -1), colors.HexColor('#1a1a1a')),
            ('ALIGN', (0, 0), (-1, -1), 'LEFT'),
            ('VALIGN', (0, 0), (-1, -1), 'MIDDLE'),
            ('FONTNAME', (0, 0), (0, -1), 'Helvetica-Bold'),
            ('FONTNAME', (1, 0), (1, -1), 'Helvetica'),
            ('FONTSIZE', (0, 0), (-1, -1), 11),
            ('BOTTOMPADDING', (0, 0), (-1, -1), 12),
            ('TOPPADDING', (0, 0), (-1, -1), 12),
            ('LINEBELOW', (0, 0), (-1, -1), 0.5, colors.HexColor('#cccccc')),
        ]))
        
        elements.append(t)
        elements.append(PageBreak()) 
        return elements

    def _create_health_meta_table(self, meta_lines: List[str]):
        full_text = " ".join(meta_lines)
        pairs = re.findall(r'([A-Za-z0-9_]+)=([^\s|]+)', full_text)
        
        formatted_items = [f"{k}: {v}" for k, v in pairs]
        
        while len(formatted_items) < 16:
            formatted_items.append("-")
        formatted_items = formatted_items[:16]
        
        table_data = [
            formatted_items[0:4],
            formatted_items[4:8],
            formatted_items[8:12],
            formatted_items[12:16]
        ]
        
        flowable_data = []
        for row in table_data:
            new_row = []
            for cell in row:
                new_row.append(ShrinkToFit(cell, font_name='Helvetica-Bold', font_size=8, text_color=colors.HexColor('#222222')))
            flowable_data.append(new_row)
            
        t = Table(flowable_data, colWidths=[1.8*inch]*4)
        t.setStyle(TableStyle([
            ('ALIGN', (0, 0), (-1, -1), 'CENTER'),
            ('VALIGN', (0, 0), (-1, -1), 'MIDDLE'),
            ('BOTTOMPADDING', (0, 0), (-1, -1), 6),
            ('TOPPADDING', (0, 0), (-1, -1), 6),
            ('GRID', (0, 0), (-1, -1), 0.5, colors.HexColor('#2c5aa0')),
            ('BACKGROUND', (0, 0), (-1, -1), colors.HexColor('#f0f4f7')),
        ]))
        return t

    def create_table(self, headers, data, col_widths=None, font_size=7):
        if not data:
            return Paragraph("No data available.", self.styles['EnhancedBody'])

        formatted_data = []
        header_row = []
        for h in headers:
            header_row.append(ShrinkToFit(h, font_name='Helvetica-Bold', font_size=font_size+1, text_color=colors.whitesmoke))
        formatted_data.append(header_row)
        
        for row in data:
            new_row = []
            for cell in row:
                new_row.append(ShrinkToFit(cell, font_name='Helvetica', font_size=font_size, text_color=colors.black))
            formatted_data.append(new_row)

        t = Table(formatted_data, colWidths=col_widths)
        t.setStyle(TableStyle([
            ('BACKGROUND', (0, 0), (-1, 0), colors.HexColor('#2c5aa0')),
            ('ALIGN', (0, 0), (-1, -1), 'CENTER'),
            ('VALIGN', (0, 0), (-1, -1), 'MIDDLE'),
            ('BOTTOMPADDING', (0, 0), (-1, -1), 4),
            ('TOPPADDING', (0, 0), (-1, -1), 4),
            ('LEFTPADDING', (0, 0), (-1, -1), 1),
            ('RIGHTPADDING', (0, 0), (-1, -1), 1),
            ('GRID', (0, 0), (-1, -1), 0.5, colors.HexColor('#cccccc')),
            ('ROWBACKGROUNDS', (0, 1), (-1, -1), [colors.white, colors.HexColor('#f9f9f9')]),
        ]))
        return t

    def _build_single_pass(self, doc, story):
        def on_page(canvas_obj, doc_obj):
            is_cover = (doc_obj.page == 1)
            if is_cover:
                self._draw_cover_page_border(canvas_obj, doc_obj)
            self._create_header_footer(canvas_obj, doc_obj, is_cover_page=is_cover)

        doc.build(story, onFirstPage=on_page, onLaterPages=on_page)

    def generate_pdf_chunk(self, chunk_data: Dict, output_file: str, is_first_chunk: bool, chunk_idx: int, total_chunks: int, summary_table: List = None):
        doc = SimpleDocTemplate(output_file, pagesize=letter, rightMargin=45, leftMargin=45, topMargin=110, bottomMargin=90)
        story = []

        self.apply_metadata(chunk_data["metadata"])
        self.chunk_info = f"(Part {chunk_idx+1}/{total_chunks})" if total_chunks > 1 else ""
        
        if is_first_chunk:
            story.extend(self._create_cover_page())
            story.extend(self._create_product_info_section(chunk_data["test_duration"]))
            
            if summary_table:
                story.append(Paragraph("Test Summary", self.styles['PhaseTitle']))
                m_headers = ["Port", "TX Pkts", "TX Bytes", "TX Gbps", "RX Pkts", "RX Bytes", "RX Gbps", "Good", "Bad", "Lost", "Bit Err", "BER"]
                col_w = [30, 50, 60, 40, 50, 60, 40, 50, 35, 35, 38, 34] 
                t1 = self.create_table(m_headers, summary_table, col_widths=col_w, font_size=6.5)
                story.append(t1)
                story.append(PageBreak())

        if chunk_data["phases"]:
            for phase in chunk_data["phases"]:
                
                # ==========================================
                # SAYFA 1: PORT İSTATİSTİKLERİ
                # ==========================================
                story.append(Paragraph(f"{phase['name']} (Port Statistics)", self.styles['PhaseTitle']))
                
                if phase["main_table"]:
                    m_headers = ["Port", "TX Pkts", "TX Bytes", "TX Gbps", "RX Pkts", "RX Bytes", "RX Gbps", "Good", "Bad", "Lost", "Bit Err", "BER"]
                    col_w = [30, 50, 60, 40, 50, 60, 40, 50, 35, 35, 38, 34] 
                    t1 = self.create_table(m_headers, phase["main_table"], col_widths=col_w, font_size=6.5)
                    story.append(t1)
                    story.append(Spacer(1, 10)) 

                if phase["raw_multi_table"]:
                    story.append(Paragraph("Raw Socket Multi-Target Statistics", self.styles['SubTitle']))
                    rs_headers = ["Source", "Target", "Rate", "TX Pkts", "TX Mbps", "RX Pkts", "Good", "Bad", "Lost", "Bit Err", "BER"]
                    col_w2 = [40, 45, 55, 50, 50, 50, 50, 35, 35, 50, 62]
                    t2 = self.create_table(rs_headers, phase["raw_multi_table"], col_widths=col_w2, font_size=6.5)
                    story.append(t2)
                    story.append(Spacer(1, 10))
                
                if phase["port12_table"] or phase["port13_table"]:
                    ext_headers = ["RX Pkts", "RX Mbps", "Good", "Bad", "Bit Errors", "Lost", "BER"]
                    ext_col_w = [80, 80, 80, 60, 80, 60, 82] 
                    if phase["port12_table"]:
                        story.append(Paragraph("Port 12 RX: DPDK External TX Packets", self.styles['SubTitle']))
                        story.append(self.create_table(ext_headers, phase["port12_table"], col_widths=ext_col_w, font_size=7))
                        story.append(Spacer(1, 5))
                    if phase["port13_table"]:
                        story.append(Paragraph("Port 13 RX: DPDK External TX Packets", self.styles['SubTitle']))
                        story.append(self.create_table(ext_headers, phase["port13_table"], col_widths=ext_col_w, font_size=7))
                
                story.append(PageBreak())
                
                # ==========================================
                # SAYFA 2: ASSISTANT FPGA
                # ==========================================
                story.append(Paragraph(f"{phase['name']} (ASSISTANT FPGA Monitor)", self.styles['PhaseTitle']))
                
                if phase.get("ast_meta"):
                    story.append(self._create_health_meta_table(phase["ast_meta"]))
                    story.append(Spacer(1, 15))
                    
                if phase.get("ast_table"):
                    h_headers = ["Port", "TxCnt", "RxCnt", "PolDrop", "VLDrop", "HP_Ovf", "LP_Ovf", "BE_Ovf"]
                    col_w = [40, 75, 75, 55, 55, 55, 55, 55]
                    story.append(self.create_table(h_headers, phase["ast_table"], col_widths=col_w, font_size=7.5))
                
                story.append(PageBreak())

                # ==========================================
                # SAYFA 3: MANAGER FPGA
                # ==========================================
                story.append(Paragraph(f"{phase['name']} (MANAGER FPGA Monitor)", self.styles['PhaseTitle']))
                
                if phase.get("mgr_meta"):
                    story.append(self._create_health_meta_table(phase["mgr_meta"]))
                    story.append(Spacer(1, 15))
                    
                if phase.get("mgr_table"):
                    h_headers = ["Port", "TxCnt", "RxCnt", "PolDrop", "VLDrop", "HP_Ovf", "LP_Ovf", "BE_Ovf"]
                    col_w = [40, 75, 75, 55, 55, 55, 55, 55]
                    story.append(self.create_table(h_headers, phase["mgr_table"], col_widths=col_w, font_size=7.5))
                
                story.append(PageBreak()) 

        self._build_single_pass(doc, story)

# ==========================================
# MULTIPROCESSING WORKER
# ==========================================
def worker_generate_pdf(args):
    idx, total_chunks, phases_chunk, metadata, test_dur, base_output, logo_path, summary_table = args
    
    if base_output.lower().endswith('.pdf'):
        out_file = f"{base_output[:-4]}_part{idx+1}.pdf"
    else:
        out_file = f"{base_output}_part{idx+1}.pdf"
        
    print(f"   -> [Worker {idx+1}/{total_chunks}] Basladi: {len(phases_chunk)} gecerli test isleniyor... (3 Sayfalik Duzen)")
    
    pdf_gen = PDFReportTemplate(logo_path=logo_path)
    chunk_data = {
        "metadata": metadata,
        "test_duration": test_dur,
        "phases": phases_chunk
    }
    
    pdf_gen.generate_pdf_chunk(chunk_data, out_file, is_first_chunk=(idx==0), chunk_idx=idx, total_chunks=total_chunks, summary_table=summary_table)
    print(f"   ✓ [Worker {idx+1}/{total_chunks}] Tamamlandi: {out_file}")
    return out_file

# ==========================================
# MAIN EXECUTION
# ==========================================
def main():
    parser = argparse.ArgumentParser(description='Generate Turbo Optimized PDF from Massive DPDK Log')
    parser.add_argument('-i', '--input', required=True, help='Absolute path to the input log file (e.g., /Home/User/dpdk.log)')
    parser.add_argument('-o', '--output', default='dtn.pdf', help='Output PDF filename')
    parser.add_argument('--logo', default=DEFAULT_LOGO_PATH, help='Path to logo image')
    parser.add_argument('--chunk-size', type=int, default=2500, help='Number of tests per PDF file (Default: 2500)')
    args = parser.parse_args()

    input_file = args.input

    if not os.path.exists(input_file):
        print(f"HATA: Belirtilen log dosyasi bulunamadi: '{input_file}'")
        sys.exit(1)

    start_time = datetime.now()
    print(f"[{start_time.strftime('%H:%M:%S')}] Parsing log file: {input_file}")
    
    log_parser = LogParser(input_file)
    parsed_data = log_parser.parse()
    
    total_phases = len(parsed_data['phases'])
    
    formatted_duration = format_duration(parsed_data.get('test_duration', 'N/A'))
    
    summary_table = None
    if parsed_data["phases"]:
        summary_table = parsed_data["phases"][-1].get("main_table")
    
    print(f"[{datetime.now().strftime('%H:%M:%S')}] Parsed data summary:")
    print(f"   - Metadata Fields: {len(parsed_data['metadata'])}")
    print(f"   - Identified Test Duration: {formatted_duration}")
    print(f"   - Total Valid Test Phases (with Health Stats): {total_phases}")
    print(f"   - Validation Reference Phase: {parsed_data.get('reference_phase_name')}")
    print(f"   - Calculated Test Result: {parsed_data['metadata'].get('Test Result', 'Unknown')} (Row-by-Row Matching)")
    
    if parsed_data.get("mismatches"):
        print("   - DETAY (Neden Fail Verildi?):")
        for m in parsed_data["mismatches"]:
            print(f"       * {m}")

    if total_phases == 0:
        print(f"[{datetime.now().strftime('%H:%M:%S')}] UYARI: Log dosyasında geçerli Health verisi bulunan test bulunamadı. PDF oluşturulmuyor.")
        sys.exit(0)

    print(f"[{datetime.now().strftime('%H:%M:%S')}] Generating PDF in Batches (Chunk Size: {args.chunk_size})...")
    
    phases = parsed_data["phases"]
    chunks = [phases[i:i + args.chunk_size] for i in range(0, len(phases), args.chunk_size)]
    total_chunks = len(chunks)
    
    worker_tasks = []
    for i, chunk in enumerate(chunks):
        worker_tasks.append((i, total_chunks, chunk, parsed_data["metadata"], formatted_duration, args.output, args.logo, summary_table))

    cpu_cores = max(1, mp.cpu_count() - 1)
    print(f"[{datetime.now().strftime('%H:%M:%S')}] {cpu_cores} islemci cekirdegi ile {total_chunks} adet PDF parcasi paralel uretiliyor...")

    with mp.Pool(processes=cpu_cores) as pool:
        generated_parts = pool.map(worker_generate_pdf, worker_tasks)

    try:
        from pypdf import PdfWriter
        print(f"[{datetime.now().strftime('%H:%M:%S')}] pypdf kutuphanesi bulundu. Parcalar tek PDF'te birlestiriliyor...")
        merger = PdfWriter()
        for pdf in generated_parts:
            merger.append(pdf)
        merger.write(args.output)
        merger.close()
        
        for pdf in generated_parts:
            os.remove(pdf)
        print(f"[{datetime.now().strftime('%H:%M:%S')}] Birlestirme basarili ve gecici parcalar silindi.")
    except ImportError:
        print(f"[{datetime.now().strftime('%H:%M:%S')}] UYARI: 'pypdf' yuklu olmadigi icin parcalar ayri PDF dosyasi olarak birakildi.")
        print("Birlestirmek icin: pip install pypdf")

    end_time = datetime.now()
    duration = (end_time - start_time).total_seconds()
    print(f"[{end_time.strftime('%H:%M:%S')}] SUCCESS! Islem tamamlandi. Suren zaman: {duration:.2f} saniye!")

if __name__ == '__main__':
    main()