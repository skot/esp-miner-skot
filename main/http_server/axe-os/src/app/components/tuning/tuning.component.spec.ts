import { CommonModule } from '@angular/common';
import { ComponentFixture, TestBed } from '@angular/core/testing';
import { of } from 'rxjs';
import { TooltipModule } from 'primeng/tooltip';

import { AsicTuningStatus } from 'src/app/generated/models';
import { PrimeNGModule } from 'src/app/prime-ng.module';
import { SystemApiService } from 'src/app/services/system.service';
import { TuningComponent } from './tuning.component';

describe('TuningComponent', () => {
  let component: TuningComponent;
  let fixture: ComponentFixture<TuningComponent>;
  let systemService: jasmine.SpyObj<SystemApiService>;

  const idleStatus: AsicTuningStatus = {
    supported: true,
    validated: false,
    state: 'idle',
    progress: 0,
    scanId: 0,
    chipCount: 4,
    coreCount: 156,
    leadingZeros: 24,
    runtimeSeconds: 4.02653184,
    message: 'Ready',
    chips: []
  };

  beforeEach(async () => {
    systemService = jasmine.createSpyObj<SystemApiService>('SystemApiService', ['getTuningStatus', 'startTuning']);
    systemService.getTuningStatus.and.returnValue(of(idleStatus));
    systemService.startTuning.and.returnValue(of({
      ...idleStatus,
      state: 'measuring',
      progress: 5,
      scanId: 1,
      message: 'Measuring core PASS counters'
    }));

    await TestBed.configureTestingModule({
      declarations: [TuningComponent],
      imports: [CommonModule, PrimeNGModule, TooltipModule],
      providers: [{ provide: SystemApiService, useValue: systemService }]
    }).compileComponents();

    fixture = TestBed.createComponent(TuningComponent);
    component = fixture.componentInstance;
    fixture.detectChanges();
  });

  afterEach(() => component.ngOnDestroy());

  it('loads the current tuning status', () => {
    expect(systemService.getTuningStatus).toHaveBeenCalled();
    expect(component.canStart).toBeTrue();
  });

  it('starts a one-shot core scan', () => {
    component.startScan();

    expect(systemService.startTuning).toHaveBeenCalled();
    expect(component.status?.state).toBe('measuring');
    expect(component.canStart).toBeFalse();
  });

  it('shows hardware scan errors', () => {
    (component as any).setStatus({
      ...idleStatus,
      state: 'error',
      progress: 75,
      message: 'Could not read MC3 per-core SPDLOG counters'
    });
    fixture.detectChanges();

    expect(fixture.nativeElement.textContent).toContain('Could not read MC3 per-core SPDLOG counters');
  });

  it('renders all 156 core results for the selected ASIC', () => {
    const passCounts = Array.from({ length: 156 }, () => 768);
    (component as any).setStatus({
      ...idleStatus,
      state: 'complete',
      progress: 100,
      validated: true,
      chips: [{
        chipId: 0,
        validated: true,
        globalPass: 119808,
        globalFail: 500,
        corePassSum: 119808,
        globalHashrate: 499.2,
        matchPercent: 100,
        corePassCounts: passCounts
      }]
    });
    fixture.detectChanges();

    expect(fixture.nativeElement.querySelectorAll('.core-cell').length).toBe(156);
  });

  it('colors core results by deviation from the ASIC mean', () => {
    const passCounts = Array.from({ length: 156 }, () => 200);
    passCounts[0] = 100;
    passCounts[1] = 300;
    (component as any).setStatus({
      ...idleStatus,
      state: 'complete',
      progress: 100,
      validated: true,
      chips: [{
        chipId: 0,
        validated: true,
        globalPass: 31200,
        globalFail: 500,
        corePassSum: 31200,
        globalHashrate: 130,
        matchPercent: 100,
        corePassCounts: passCounts
      }]
    });
    fixture.detectChanges();

    const cells = fixture.nativeElement.querySelectorAll('.core-cell');
    expect(cells[0].classList).toContain('below-mean');
    expect(cells[1].classList).toContain('above-mean');
    expect(cells[2].classList).toContain('at-mean');
    expect(cells[0].style.backgroundColor).toBe('rgba(239, 68, 68, 0.75)');
    expect(cells[1].style.backgroundColor).toBe('rgba(34, 197, 94, 0.75)');
    expect(fixture.nativeElement.textContent).toContain('Mean Core');
  });
});
