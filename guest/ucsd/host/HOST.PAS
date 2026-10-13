PROGRAM HOST;
(*$I-*)

TYPE
  TByte = 0..255;
  TQuad = PACKED ARRAY[1..4] OF TByte;
  TCommand = STRING[80];
  TBlock = PACKED ARRAY[0..511] OF CHAR;
  TDirBuf = PACKED ARRAY[0..2047] OF CHAR;
  TFileNm = STRING[15];
  TPage = PACKED ARRAY[0..1023] OF CHAR;
  TLine = PACKED ARRAY[1..201] OF CHAR;

VAR
  CmdLine: TCommand;
  Verb: TCommand;
  Argument: TCommand;
  Option: TCommand;
  BlkFile: FILE;
  PendName: ARRAY[1..24] OF TFileNm;
  PendTail: ARRAY[1..24] OF INTEGER;
  PendCnt: INTEGER;
  BlkBuf: TBlock;
  DirBuf: TDirBuf;
  CrcValue: TQuad;
  ExpCrc: TQuad;
  FileSize: TQuad;
  IoError: BOOLEAN;
  ExchOK: BOOLEAN;
  QuitReq: BOOLEAN;
  FileKind: INTEGER;
  (*-- GET T state: Pascal TEXT pages are built in PageBuf, one line at a time in LineBuf *)
  PageBuf: TPage;
  PageLen: INTEGER;
  LineBuf: TLine;
  LineLen: INTEGER;
  PrevCR: BOOLEAN;
  BadChars: INTEGER;
  LongLine: BOOLEAN;
  (*-- PUT T state: converted bytes are collected in OutBuf and counted in OutSize *)
  OutBuf: TBlock;
  OutLen: INTEGER;
  OutSize: TQuad;
  OutSend: BOOLEAN;
  CrLfEol: BOOLEAN;
  DleSeen: BOOLEAN;
  SkipRest: BOOLEAN;

FUNCTION EXCHREAD: INTEGER; EXTERNAL;
PROCEDURE EXCHWRT(Value: INTEGER); EXTERNAL;
PROCEDURE EXCHABT; EXTERNAL;
PROCEDURE EXCHRDB(VAR Buf: TBlock; Count: INTEGER); EXTERNAL;
PROCEDURE EXCHWRB(VAR Buf: TBlock; Count: INTEGER); EXTERNAL;
PROCEDURE EXCHCRC(VAR Crc: TQuad; VAR Buf: TBlock; Count: INTEGER); EXTERNAL;

PROCEDURE InitCrc(VAR Value: TQuad);
VAR
  Counter: INTEGER;
BEGIN
  FOR Counter := 1 TO 4 DO
    Value[Counter] := 255
END;

PROCEDURE EndCrc(VAR Value: TQuad);
VAR
  Counter: INTEGER;
BEGIN
  FOR Counter := 1 TO 4 DO
    Value[Counter] := 255 - Value[Counter]
END;

PROCEDURE ZeroQuad(VAR Value: TQuad);
VAR
  Counter: INTEGER;
BEGIN
  FOR Counter := 1 TO 4 DO
    Value[Counter] := 0
END;

FUNCTION SameQuad(LeftVal, RightVal: TQuad): BOOLEAN;
VAR
  Counter: INTEGER;
BEGIN
  SameQuad := TRUE;
  FOR Counter := 1 TO 4 DO
    IF LeftVal[Counter] <> RightVal[Counter] THEN
      SameQuad := FALSE
END;

PROCEDURE SendQuad(Value: TQuad);
VAR
  Counter: INTEGER;
BEGIN
  FOR Counter := 1 TO 4 DO
    EXCHWRT(Value[Counter])
END;

PROCEDURE RecvQuad(VAR Value: TQuad);
VAR
  Counter: INTEGER;
BEGIN
  FOR Counter := 1 TO 4 DO
    Value[Counter] := EXCHREAD
END;

PROCEDURE LenQuad(Blocks, LastByt: INTEGER; VAR Value: TQuad);
VAR
  Full: INTEGER;
  Low: INTEGER;
  High: INTEGER;
BEGIN
  IF Blocks = 0 THEN
    ZeroQuad(Value)
  ELSE
  BEGIN
    Full := (Blocks - 1) * 2;
    Low := (Full MOD 256) + (LastByt DIV 256);
    High := Full DIV 256;
    IF Low >= 256 THEN
    BEGIN
      Low := Low - 256;
      High := High + 1
    END;
    Value[1] := LastByt MOD 256;
    Value[2] := Low;
    Value[3] := High MOD 256;
    Value[4] := High DIV 256
  END
END;

PROCEDURE AddQuad(VAR Value: TQuad; Amount: INTEGER);
VAR
  Counter: INTEGER;
  Sum: INTEGER;
  Carry: INTEGER;
BEGIN
  Carry := Amount;
  FOR Counter := 1 TO 4 DO
  BEGIN
    Sum := Value[Counter] + (Carry MOD 256);
    Carry := Carry DIV 256;
    IF Sum >= 256 THEN
    BEGIN
      Sum := Sum - 256;
      Carry := Carry + 1
    END;
    Value[Counter] := Sum
  END
END;

PROCEDURE QuadParts(Value: TQuad; VAR Chunks: INTEGER; VAR Rest: INTEGER;
                    VAR Fits: BOOLEAN);
BEGIN
  Fits := (Value[4] = 0) AND (Value[3] < 128);
  Rest := Value[1];
  Chunks := 0;
  IF Fits THEN
    Chunks := Value[2] + (256 * Value[3])
END;

FUNCTION UpperChr(Value: CHAR): CHAR;
BEGIN
  IF (Value >= 'a') AND (Value <= 'z') THEN
    UpperChr := CHR(ORD(Value) - 32)
  ELSE
    UpperChr := Value
END;

FUNCTION IsTextNm(Name: TCommand): BOOLEAN;
BEGIN
  IsTextNm := FALSE;
  IF LENGTH(Name) > 5 THEN
    IsTextNm := COPY(Name, LENGTH(Name) - 4, 5) = '.TEXT'
END;

PROCEDURE UpString(VAR Value: TCommand);
VAR
  Counter: INTEGER;
BEGIN
  FOR Counter := 1 TO LENGTH(Value) DO
    Value[Counter] := UpperChr(Value[Counter])
END;

FUNCTION ValidNm(Value: TCommand): BOOLEAN;
VAR
  Counter: INTEGER;
  CharVal: CHAR;
  IsValid: BOOLEAN;
BEGIN
  IsValid := (LENGTH(Value) >= 1) AND (LENGTH(Value) <= 15);
  IF IsValid THEN
  BEGIN
    IF NOT ((Value[1] >= 'A') AND (Value[1] <= 'Z')) THEN
      IsValid := FALSE;
    FOR Counter := 1 TO LENGTH(Value) DO
    BEGIN
      CharVal := Value[Counter];
      IF NOT (((CharVal >= 'A') AND (CharVal <= 'Z')) OR
              ((CharVal >= '0') AND (CharVal <= '9')) OR
              (CharVal = '.')) THEN
        IsValid := FALSE
    END
  END;
  ValidNm := IsValid
END;

PROCEDURE GetToken(VAR Text: TCommand; VAR Cursor: INTEGER;
                   VAR Token: TCommand);
VAR
  Done: BOOLEAN;
BEGIN
  Token := '';
  Done := FALSE;
  WHILE (Cursor <= LENGTH(Text)) AND NOT Done DO
  BEGIN
    IF Text[Cursor] = ' ' THEN
      Cursor := Cursor + 1
    ELSE
      Done := TRUE
  END;
  Done := FALSE;
  WHILE (Cursor <= LENGTH(Text)) AND NOT Done DO
  BEGIN
    IF Text[Cursor] = ' ' THEN
      Done := TRUE
    ELSE
    BEGIN
      Token := CONCAT(Token, COPY(Text, Cursor, 1));
      Cursor := Cursor + 1
    END
  END;
END;

PROCEDURE PrStatus(Status: INTEGER);
BEGIN
  CASE Status OF
    1: WRITELN('Invalid name or request.');
    2: WRITELN('File or exchange directory unavailable.');
    3: WRITELN('Destination already exists.');
    4: WRITELN('I/O or checksum error.');
    5: WRITELN('Exchange service busy.')
  OTHERWISE
    WRITELN('Unknown exchange error.')
  END
END;

PROCEDURE QueryExc(VAR Good: BOOLEAN);
VAR
  Status: INTEGER;
  Version: INTEGER;
  Capabil: INTEGER;
BEGIN
  EXCHWRT(0);
  Status := EXCHREAD;
  Version := EXCHREAD;
  Capabil := EXCHREAD;
  Good := (Status = 0) AND (Version = 1) AND ((Capabil MOD 8) = 7);
  IF NOT Good THEN
  BEGIN
    WRITELN('HOST exchange protocol is unavailable or unsupported.');
    IF Status <> 0 THEN
      PrStatus(Status)
  END
END;

PROCEDURE ListXchg;
VAR
  Status: INTEGER;
  CharVal: INTEGER;
  Count: INTEGER;
  NameLength: INTEGER;
BEGIN
  EXCHWRT(1);
  Status := EXCHREAD;
  IF Status <> 0 THEN
  BEGIN
    WRITE('DIR: ');
    PrStatus(Status);
  END;
  IF Status = 0 THEN
  BEGIN
    Count := 0;
    REPEAT
      NameLength := 0;
      CharVal := EXCHREAD;
      WHILE CharVal <> 0 DO
      BEGIN
        WRITE(CHR(CharVal));
        NameLength := NameLength + 1;
        CharVal := EXCHREAD
      END;
      IF NameLength > 0 THEN
      BEGIN
        WRITELN;
        Count := Count + 1
      END
    UNTIL NameLength = 0;
    Status := EXCHREAD;
    IF Status <> 0 THEN
    BEGIN
      WRITE('DIR: ');
      PrStatus(Status)
    END;
    IF (Status = 0) AND (Count = 0) THEN
      WRITELN('No files.')
  END
END;


PROCEDURE FindEntry(Name: TCommand; VAR Found: BOOLEAN; VAR Base: INTEGER);
VAR
  Counter: INTEGER;
  Index: INTEGER;
  NumFiles: INTEGER;
  Same: BOOLEAN;
BEGIN
  Found := FALSE;
  Base := 0;
  NumFiles := ORD(DirBuf[16]) + (256 * ORD(DirBuf[17]));
  IF NumFiles > 77 THEN
    NumFiles := 77;
  Counter := 1;
  WHILE (Counter <= NumFiles) AND NOT Found DO
  BEGIN
    Base := Counter * 26;
    Same := ORD(DirBuf[Base + 6]) = LENGTH(Name);
    Index := 1;
    WHILE Same AND (Index <= LENGTH(Name)) DO
    BEGIN
      IF DirBuf[Base + 6 + Index] <> Name[Index] THEN
        Same := FALSE;
      Index := Index + 1
    END;
    IF Same THEN
      Found := TRUE;
    Counter := Counter + 1
  END
END;

PROCEDURE FileInfo(Name: TCommand; VAR Found: BOOLEAN; VAR Blocks: INTEGER;
                   VAR LastByt: INTEGER; VAR Good: BOOLEAN);
VAR
  Base: INTEGER;
  Got: INTEGER;
BEGIN
  Found := FALSE;
  Good := FALSE;
  Blocks := 0;
  LastByt := 0;
  FileKind := 0;
  RESET(BlkFile, ':');
  IF IORESULT = 0 THEN
  BEGIN
    Got := BLOCKREAD(BlkFile, DirBuf, 4, 2);
    IF (Got = 4) AND (IORESULT = 0) THEN
    BEGIN
      Good := TRUE;
      FindEntry(Name, Found, Base);
      IF Found THEN
      BEGIN
        Blocks := (ORD(DirBuf[Base + 2]) + (256 * ORD(DirBuf[Base + 3]))) -
                  (ORD(DirBuf[Base]) + (256 * ORD(DirBuf[Base + 1])));
        LastByt := ORD(DirBuf[Base + 22]) + (256 * ORD(DirBuf[Base + 23]));
        FileKind := ORD(DirBuf[Base + 4]) MOD 16
      END
    END;
    CLOSE(BlkFile)
  END
END;

PROCEDURE ScanFile(Name: TCommand; Blocks, LastByt: INTEGER; Send: BOOLEAN);
VAR
  BlockNo: INTEGER;
  Count: INTEGER;
  Got: INTEGER;
BEGIN
  IoError := FALSE;
  IF NOT Send THEN
    InitCrc(CrcValue);
  RESET(BlkFile, Name);
  IF IORESULT <> 0 THEN
    IoError := TRUE
  ELSE
  BEGIN
    BlockNo := 1;
    WHILE (BlockNo <= Blocks) AND (NOT IoError) DO
    BEGIN
      Got := BLOCKREAD(BlkFile, BlkBuf, 1);
      IF (Got <> 1) OR (IORESULT <> 0) THEN
        IoError := TRUE
      ELSE
      BEGIN
        IF BlockNo = Blocks THEN
          Count := LastByt
        ELSE
          Count := 512;
        IF Send THEN
          EXCHWRB(BlkBuf, Count)
        ELSE
          EXCHCRC(CrcValue, BlkBuf, Count);
        BlockNo := BlockNo + 1
      END
    END;
    CLOSE(BlkFile);
    IF IORESULT <> 0 THEN
      IoError := TRUE
  END;
  IF (NOT Send) AND (NOT IoError) THEN
    EndCrc(CrcValue)
END;

PROCEDURE FlushOut;
BEGIN
  IF OutLen > 0 THEN
  BEGIN
    IF OutSend THEN
      EXCHWRB(OutBuf, OutLen)
    ELSE
      EXCHCRC(CrcValue, OutBuf, OutLen);
    AddQuad(OutSize, OutLen);
    OutLen := 0
  END
END;

PROCEDURE EmitOut(Value: CHAR);
BEGIN
  OutBuf[OutLen] := Value;
  OutLen := OutLen + 1;
  IF OutLen = 512 THEN
    FlushOut
END;

PROCEDURE EmitEol;
BEGIN
  IF CrLfEol THEN
    EmitOut(CHR(13));
  EmitOut(CHR(10))
END;

(*-- Decodes one 512-byte block of a TEXT page: DLE n = n-32 spaces, CR = end of line, NUL = page padding *)
PROCEDURE UnpackBlk(PageStart: BOOLEAN);
VAR
  Index: INTEGER;
  Counter: INTEGER;
  Value: CHAR;
BEGIN
  IF PageStart THEN
  BEGIN
    SkipRest := FALSE;
    DleSeen := FALSE
  END;
  FOR Index := 0 TO 511 DO
  BEGIN
    Value := BlkBuf[Index];
    IF NOT SkipRest THEN
    BEGIN
      IF DleSeen THEN
      BEGIN
        DleSeen := FALSE;
        FOR Counter := 33 TO ORD(Value) DO
          EmitOut(' ')
      END
      ELSE IF ORD(Value) = 0 THEN
        SkipRest := TRUE
      ELSE IF ORD(Value) = 16 THEN
        DleSeen := TRUE
      ELSE IF ORD(Value) = 13 THEN
        EmitEol
      ELSE
        EmitOut(Value)
    END
  END
END;

(*-- Converts a Pascal TEXT file to OS text. Pass 1 (Send = FALSE) gives the size and CRC, pass 2 sends it *)
PROCEDURE TextScan(Name: TCommand; Blocks: INTEGER; Send: BOOLEAN);
VAR
  BlockNo: INTEGER;
  Got: INTEGER;
BEGIN
  IoError := FALSE;
  OutSend := Send;
  OutLen := 0;
  DleSeen := FALSE;
  SkipRest := FALSE;
  ZeroQuad(OutSize);
  IF NOT Send THEN
    InitCrc(CrcValue);
  RESET(BlkFile, Name);
  IF IORESULT <> 0 THEN
    IoError := TRUE
  ELSE
  BEGIN
    BlockNo := 1;
    WHILE (BlockNo <= Blocks) AND (NOT IoError) DO
    BEGIN
      Got := BLOCKREAD(BlkFile, BlkBuf, 1);
      IF (Got <> 1) OR (IORESULT <> 0) THEN
        IoError := TRUE
      ELSE
      BEGIN
        IF BlockNo > 2 THEN
          UnpackBlk(((BlockNo - 3) MOD 2) = 0);
        BlockNo := BlockNo + 1
      END
    END;
    CLOSE(BlkFile);
    IF IORESULT <> 0 THEN
      IoError := TRUE
  END;
  IF NOT IoError THEN
    FlushOut;
  IF (NOT Send) AND (NOT IoError) THEN
    EndCrc(CrcValue)
END;

PROCEDURE NoteLast(Name: TCommand; Tail: INTEGER; VAR Good: BOOLEAN);
VAR
  Slot: INTEGER;
  Counter: INTEGER;
BEGIN
  Good := TRUE;
  Slot := 0;
  FOR Counter := 1 TO PendCnt DO
    IF PendName[Counter] = Name THEN
      Slot := Counter;
  IF (Slot = 0) AND (Tail > 0) THEN
  BEGIN
    IF PendCnt < 24 THEN
    BEGIN
      PendCnt := PendCnt + 1;
      Slot := PendCnt;
      PendName[Slot] := Name
    END
    ELSE
      Good := FALSE
  END;
  IF Slot > 0 THEN
  BEGIN
    IF Tail > 0 THEN
      PendTail[Slot] := Tail
    ELSE
      PendTail[Slot] := 512
  END
END;

PROCEDURE ApplyLast(VAR Good: BOOLEAN);
VAR
  Found: BOOLEAN;
  Blocks: INTEGER;
  LastByt: INTEGER;
  Counter: INTEGER;
  Base: INTEGER;
  Got: INTEGER;
BEGIN
  Good := TRUE;
  IF PendCnt > 0 THEN
  BEGIN
    FileInfo('', Found, Blocks, LastByt, Good);
    IF Good THEN
    BEGIN
      FOR Counter := 1 TO PendCnt DO
      BEGIN
        FindEntry(PendName[Counter], Found, Base);
        IF Found THEN
        BEGIN
          DirBuf[Base + 22] := CHR(PendTail[Counter] MOD 256);
          DirBuf[Base + 23] := CHR(PendTail[Counter] DIV 256)
        END
      END;
      RESET(BlkFile, ':');
      Good := IORESULT = 0;
      IF Good THEN
      BEGIN
        Got := BLOCKWRITE(BlkFile, DirBuf, 4, 2);
        Good := (Got = 4) AND (IORESULT = 0);
        CLOSE(BlkFile);
        IF IORESULT <> 0 THEN
          Good := FALSE
      END
    END
  END
END;

PROCEDURE FlushPage;
VAR
  Got: INTEGER;
BEGIN
  IF PageLen > 0 THEN
  BEGIN
    FILLCHAR(PageBuf[PageLen], 1024 - PageLen, CHR(0));
    Got := BLOCKWRITE(BlkFile, PageBuf, 2);
    IF (Got <> 2) OR (IORESULT <> 0) THEN
      IoError := TRUE;
    PageLen := 0
  END
END;

(*-- Stores LineBuf as DLE, 32+indent, text, CR. A line never crosses a page and a page keeps one NUL *)
PROCEDURE PutLine;
VAR
  Indent: INTEGER;
  Counter: INTEGER;
BEGIN
  Indent := 0;
  WHILE (Indent < LineLen) AND (LineBuf[Indent + 1] = ' ') DO
    Indent := Indent + 1;
  IF PageLen + (LineLen - Indent) + 4 > 1024 THEN
    FlushPage;
  PageBuf[PageLen] := CHR(16);
  PageBuf[PageLen + 1] := CHR(32 + Indent);
  PageLen := PageLen + 2;
  FOR Counter := Indent + 1 TO LineLen DO
  BEGIN
    PageBuf[PageLen] := LineBuf[Counter];
    PageLen := PageLen + 1
  END;
  PageBuf[PageLen] := CHR(13);
  PageLen := PageLen + 1;
  LineLen := 0
END;

PROCEDURE AddChar(Value: CHAR);
BEGIN
  IF LineLen >= 200 THEN
    LongLine := TRUE
  ELSE
  BEGIN
    LineLen := LineLen + 1;
    LineBuf[LineLen] := Value
  END
END;

(*-- LF, CRLF and a lone CR all end a line; TAB goes to the next multiple of 8; other controls become ? *)
PROCEDURE ConvChar(Value: CHAR);
BEGIN
  IF PrevCR AND (ORD(Value) = 10) THEN
    PrevCR := FALSE
  ELSE
  BEGIN
    PrevCR := ORD(Value) = 13;
    IF (ORD(Value) = 13) OR (ORD(Value) = 10) THEN
      PutLine
    ELSE IF ORD(Value) = 9 THEN
    BEGIN
      AddChar(' ');
      WHILE (LineLen MOD 8) <> 0 DO
        AddChar(' ')
    END
    ELSE IF (ORD(Value) < 32) OR (ORD(Value) > 126) THEN
    BEGIN
      BadChars := BadChars + 1;
      AddChar('?')
    END
    ELSE
      AddChar(Value)
  END
END;

PROCEDURE ConvBlock(Count: INTEGER);
VAR
  Index: INTEGER;
BEGIN
  FOR Index := 0 TO Count - 1 DO
    ConvChar(BlkBuf[Index]);
  IF LongLine THEN
    IoError := TRUE
END;

PROCEDURE StartText;
VAR
  Got: INTEGER;
BEGIN
  PageLen := 0;
  LineLen := 0;
  PrevCR := FALSE;
  BadChars := 0;
  FILLCHAR(PageBuf, 1024, CHR(0));
  Got := BLOCKWRITE(BlkFile, PageBuf, 2);
  IF (Got <> 2) OR (IORESULT <> 0) THEN
    IoError := TRUE
END;

PROCEDURE FinishText;
BEGIN
  IF LineLen > 0 THEN
    PutLine;
  FlushPage
END;

PROCEDURE RecvPart(Count: INTEGER; TextMode: BOOLEAN);
VAR
  Got: INTEGER;
BEGIN
  IF Count < 512 THEN
    FILLCHAR(BlkBuf, 512, CHR(0));
  EXCHRDB(BlkBuf, Count);
  EXCHCRC(CrcValue, BlkBuf, Count);
  IF TextMode THEN
    ConvBlock(Count)
  ELSE
  BEGIN
    Got := BLOCKWRITE(BlkFile, BlkBuf, 1);
    IF (Got <> 1) OR (IORESULT <> 0) THEN
      IoError := TRUE
  END
END;

PROCEDURE RecvFile(Name: TCommand; Overwrte, TextMode: BOOLEAN);
VAR
  Status: INTEGER;
  Chunks: INTEGER;
  Rest: INTEGER;
  Counter: INTEGER;
  FullBlks: INTEGER;
  Tail: INTEGER;
  Blocks: INTEGER;
  LastByt: INTEGER;
  Found: BOOLEAN;
  Good: BOOLEAN;
  Fits: BOOLEAN;
  CanContinue: BOOLEAN;
  Touched: BOOLEAN;
  Stored: BOOLEAN;
  Applied: BOOLEAN;
  Noted: BOOLEAN;
BEGIN
  CanContinue := TRUE;
  Touched := FALSE;
  Stored := FALSE;
  Noted := TRUE;
  FileInfo(Name, Found, Blocks, LastByt, Good);
  IF NOT Good THEN
  BEGIN
    WRITELN('GET: cannot check the local destination.');
    CanContinue := FALSE
  END
  ELSE IF Found AND NOT Overwrte THEN
  BEGIN
    WRITELN('GET: destination already exists; specify O to replace it.');
    CanContinue := FALSE
  END;
  IF CanContinue THEN
  BEGIN
    EXCHWRT(2);
    FOR Counter := 1 TO LENGTH(Name) DO
      EXCHWRT(ORD(Name[Counter]));
    EXCHWRT(0);
    Status := EXCHREAD;
    IF Status <> 0 THEN
    BEGIN
      WRITE('GET: ');
      PrStatus(Status);
      CanContinue := FALSE
    END
  END;
  IF CanContinue THEN
  BEGIN
    RecvQuad(FileSize);
    QuadParts(FileSize, Chunks, Rest, Fits);
    IF NOT Fits THEN
    BEGIN
      EXCHABT;
      WRITELN('GET: file is too large for this utility.');
      CanContinue := FALSE
    END
  END;
  IF CanContinue THEN
  BEGIN
    REWRITE(BlkFile, Name);
    Touched := TRUE;
    IF IORESULT <> 0 THEN
    BEGIN
      EXCHABT;
      WRITELN('GET: cannot create the local file.');
      CanContinue := FALSE
    END
  END;
  IF CanContinue THEN
  BEGIN
    IoError := FALSE;
    LongLine := FALSE;
    InitCrc(CrcValue);
    IF TextMode THEN
      StartText;
    FullBlks := Chunks DIV 2;
    Tail := ((Chunks MOD 2) * 256) + Rest;
    Counter := 1;
    WHILE (Counter <= FullBlks) AND (NOT IoError) DO
    BEGIN
      RecvPart(512, TextMode);
      Counter := Counter + 1
    END;
    IF (NOT IoError) AND (Tail > 0) THEN
      RecvPart(Tail, TextMode);
    IF TextMode AND (NOT IoError) THEN
      FinishText;
    IF IoError THEN
    BEGIN
      EXCHABT;
      CLOSE(BlkFile);
      IF LongLine THEN
        WRITELN('GET: a line is longer than 200 characters; nothing was stored.')
      ELSE
        WRITELN('GET: local file error; nothing was stored.')
    END
    ELSE
    BEGIN
      RecvQuad(ExpCrc);
      Status := EXCHREAD;
      EndCrc(CrcValue);
      IF Status <> 0 THEN
      BEGIN
        CLOSE(BlkFile);
        WRITE('GET: ');
        PrStatus(Status)
      END
      ELSE IF NOT SameQuad(CrcValue, ExpCrc) THEN
      BEGIN
        CLOSE(BlkFile);
        WRITELN('GET: checksum error; nothing was stored.')
      END
      ELSE
      BEGIN
        CLOSE(BlkFile, LOCK);
        IF IORESULT <> 0 THEN
          WRITELN('GET: could not store the verified file.')
        ELSE
        BEGIN
          IF NOT TextMode THEN
            NoteLast(Name, Tail, Noted);
          Stored := TRUE
        END
      END
    END
  END;
  IF Touched THEN
  BEGIN
    ApplyLast(Applied);
    IF Stored THEN
    BEGIN
      IF NOT (Applied AND Noted) THEN
        WRITELN('GET: stored, but the exact length could not be set.');
      IF TextMode AND (BadChars > 0) THEN
        WRITELN('GET: ', BadChars, ' unsupported characters replaced by ?');
      IF TextMode AND NOT IsTextNm(Name) THEN
        WRITELN('GET: note: only a name ending in .TEXT is a Pascal TEXT file.');
      WRITELN('GET ', Name, ' ... Done')
    END
    ELSE IF NOT Applied THEN
      WRITELN('GET: could not restore the exact lengths of earlier files.')
  END
END;

PROCEDURE SendFile(Name: TCommand; Overwrte, TextMode, WinEol: BOOLEAN);
VAR
  Status: INTEGER;
  Counter: INTEGER;
  Blocks: INTEGER;
  LastByt: INTEGER;
  Found: BOOLEAN;
  Good: BOOLEAN;
BEGIN
  FileInfo(Name, Found, Blocks, LastByt, Good);
  IF NOT (Good AND Found) THEN
    WRITELN('PUT: cannot read the local file.')
  ELSE IF Blocks > 16000 THEN
    WRITELN('PUT: file is too large for this utility.')
  ELSE IF TextMode AND (FileKind <> 3) THEN
    WRITELN('PUT: option T needs a Pascal TEXT file (name ending in .TEXT).')
  ELSE
  BEGIN
    CrLfEol := WinEol;
    IF TextMode THEN
    BEGIN
      TextScan(Name, Blocks, FALSE);
      FileSize := OutSize
    END
    ELSE
    BEGIN
      LenQuad(Blocks, LastByt, FileSize);
      ScanFile(Name, Blocks, LastByt, FALSE)
    END;
    IF IoError THEN
      WRITELN('PUT: cannot read the local file.')
    ELSE
    BEGIN
      IF Overwrte THEN
        EXCHWRT(4)
      ELSE
        EXCHWRT(3);
      FOR Counter := 1 TO LENGTH(Name) DO
        EXCHWRT(ORD(Name[Counter]));
      EXCHWRT(0);
      SendQuad(FileSize);
      SendQuad(CrcValue);
      Status := EXCHREAD;
      IF Status <> 0 THEN
      BEGIN
        WRITE('PUT: ');
        PrStatus(Status)
      END
      ELSE
      BEGIN
        IF TextMode THEN
          TextScan(Name, Blocks, TRUE)
        ELSE
          ScanFile(Name, Blocks, LastByt, TRUE);
        IF IoError THEN
        BEGIN
          EXCHABT;
          WRITELN('PUT: local file changed or could not be read.')
        END
        ELSE
        BEGIN
          Status := EXCHREAD;
          IF Status <> 0 THEN
          BEGIN
            WRITE('PUT: ');
            PrStatus(Status)
          END
          ELSE
            WRITELN('PUT ', Name, ' ... Done')
        END
      END
    END
  END
END;

PROCEDURE ShowUsage;
BEGIN
  WRITELN('Use DIR, GET <file> [T] [W] [O], PUT <file> [T] [W] [O], or QUIT.');
  WRITELN('  O = overwrite an existing file');
  WRITELN('  T = convert OS text <-> Pascal TEXT file');
  WRITELN('  W = with T: PUT ends lines with CR LF (default LF)')
END;

PROCEDURE RunCmd;
VAR
  Cursor: INTEGER;
  OptCount: INTEGER;
  Overwrte: BOOLEAN;
  TextMode: BOOLEAN;
  WinEol: BOOLEAN;
  OptsOK: BOOLEAN;
BEGIN
  UpString(CmdLine);
  Cursor := 1;
  Overwrte := FALSE;
  TextMode := FALSE;
  WinEol := FALSE;
  OptsOK := TRUE;
  OptCount := 0;
  GetToken(CmdLine, Cursor, Verb);
  GetToken(CmdLine, Cursor, Argument);
  GetToken(CmdLine, Cursor, Option);
  WHILE Option <> '' DO
  BEGIN
    OptCount := OptCount + 1;
    IF (Option = 'O') AND NOT Overwrte THEN
      Overwrte := TRUE
    ELSE IF (Option = 'T') AND NOT TextMode THEN
      TextMode := TRUE
    ELSE IF (Option = 'W') AND NOT WinEol THEN
      WinEol := TRUE
    ELSE
      OptsOK := FALSE;
    GetToken(CmdLine, Cursor, Option)
  END;
  IF Verb = '' THEN
    ShowUsage
  ELSE IF (Verb = 'QUIT') OR (Verb = 'Q') THEN
    QuitReq := TRUE
  ELSE IF Verb = 'DIR' THEN
  BEGIN
    IF (Argument <> '') OR (OptCount > 0) THEN
      WRITELN('Usage: DIR')
    ELSE
      ListXchg
  END
  ELSE IF (Verb <> 'GET') AND (Verb <> 'PUT') THEN
    ShowUsage
  ELSE IF NOT ValidNm(Argument) THEN
    WRITELN('Invalid filename.')
  ELSE IF OptCount > 3 THEN
    WRITELN('Too many command arguments.')
  ELSE IF NOT OptsOK THEN
    WRITELN('The options are O, T and W; use each at most once.')
  ELSE IF WinEol AND NOT TextMode THEN
    WRITELN('Option W needs option T.')
  ELSE IF Verb = 'GET' THEN
    RecvFile(Argument, Overwrte, TextMode)
  ELSE
    SendFile(Argument, Overwrte, TextMode, WinEol)
END;

BEGIN
  PendCnt := 0;
  QuitReq := FALSE;
  QueryExc(ExchOK);
  IF ExchOK THEN
  BEGIN
    WRITELN('HOST file exchange');
    REPEAT
      WRITE('HOST> ');
      READLN(CmdLine);
      RunCmd
    UNTIL QuitReq
  END
END.
